"""Append-only session CSVs and sensor-time-aligned offline evaluation (no Qt)."""
from contextlib import ExitStack
from dataclasses import asdict
from datetime import datetime, timezone
from pathlib import Path
import csv
import json
import math
import uuid

from .protocol import STATUS_FIELDS, TrackReportResult, TrackState


def session_path(base):
    return Path(base) / (datetime.now().strftime('%Y%m%d-%H%M%S-') + uuid.uuid4().hex[:8])


class SessionRecorder:
    """One serial-owner context writes a session; existing paths are never overwritten."""

    def __init__(self, directory, settings, mode):
        self.path = Path(directory)
        self.path.mkdir(parents=True, exist_ok=False)
        self.metadata = {'schema_version': 1, 'mode': mode, 'settings': asdict(settings),
                         'started_utc': datetime.now(timezone.utc).isoformat(), 'closed': False}
        self._stack = ExitStack()
        self._files = {}
        self._writers = {}
        try:
            self._metadata()
            fields = {
                'samples': ['timestamp_ms', 'truth_range_m', 'truth_azimuth_rad', 'measured_range_m',
                            'measured_azimuth_rad', 'tx_monotonic_s', 'frame_hex'],
                'reports': ['report_sequence', 'input_timestamp_ms', 'estimate_timestamp_ms',
                            'range_m', 'azimuth_rad', 'range_rate_mps', 'azimuth_rate_radps',
                            'state', 'result', 'flags', 'rx_monotonic_s'],
                'wire': ['direction', 'monotonic_s', 'hex'],
                'status': ['rx_monotonic_s'] + [field[0] for field in STATUS_FIELDS],
            }
            for name, columns in fields.items():
                stream = self._stack.enter_context((self.path / f'{name}.csv').open('w', newline='', encoding='utf-8'))
                self._files[name] = stream
                writer = csv.DictWriter(stream, fieldnames=columns)
                writer.writeheader()
                self._writers[name] = writer
                stream.flush()
        except BaseException:
            self._stack.close()
            raise

    def _metadata(self):
        (self.path / 'metadata.json').write_text(json.dumps(self.metadata, indent=2) + '\n', encoding='utf-8')

    def _row(self, name, row):
        self._writers[name].writerow(row)
        self._files[name].flush()

    def record_sample(self, sample, sent_at):
        self._row('samples', {'timestamp_ms': sample.timestamp_ms, 'truth_range_m': sample.truth_range_m,
                  'truth_azimuth_rad': math.radians(self.metadata['settings']['azimuth_deg']),
                  'measured_range_m': sample.measured_range_m, 'measured_azimuth_rad': sample.azimuth_rad,
                  'tx_monotonic_s': sent_at, 'frame_hex': sample.frame.hex()})

    def record_report(self, report, received_at):
        self._row('reports', {**asdict(report), 'rx_monotonic_s': received_at})

    def record_wire(self, direction, raw, now):
        if raw:
            self._row('wire', {'direction': direction, 'monotonic_s': now, 'hex': raw.hex()})

    def record_status(self, stats, received_at):
        self._row('status', {'rx_monotonic_s': received_at, **stats})

    def close(self):
        if not self.metadata['closed']:
            self._stack.close()
            self.metadata['closed'] = True
            self._metadata()

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()


def evaluate_session(directory, warmup_ms=2000):
    """Compare only valid, unique estimate timestamps with matching sent samples.

    Warm-up restarts on STARTED. Invalid events, unmatched timestamps, and repeated
    estimate times are counted but excluded. PC roundtrip includes transport/host
    scheduling and is not a measurement of MCU processing time.
    """
    path = Path(directory)
    metadata = json.loads((path / 'metadata.json').read_text(encoding='utf-8'))
    if metadata['mode'] != 'board':
        raise ValueError('Evaluation requires a board recording')
    if type(warmup_ms) is not int or warmup_ms < 0:
        raise ValueError('warmup_ms must be a nonnegative integer')
    def rows(name):
        with (path / f'{name}.csv').open(newline='', encoding='utf-8') as stream:
            return list(csv.DictReader(stream))
    sample_rows = rows('samples')
    samples = {int(row['timestamp_ms']): row for row in sample_rows}
    if len(samples) != len(sample_rows):
        raise ValueError('Repeated sensor timestamps make this session ambiguous')
    reports = rows('reports')
    result = {'sent_samples': len(samples), 'received_reports': len(reports), 'warmup_ms': warmup_ms,
              'matched_estimates': 0, 'invalid_estimates': 0, 'unmatched_estimates': 0,
              'duplicate_estimate_times': 0, 'warmup_excluded': 0, 'sequence_gaps': 0,
              'sequence_discontinuities': 0, 'recording_closed': metadata['closed']}
    errors = {key: [] for key in ('range_measurement_rmse_m', 'range_estimate_rmse_m',
                                  'azimuth_measurement_rmse_rad', 'azimuth_estimate_rmse_rad')}
    result.update(gate_rejected=0, time_rejected=0, track_starts=0, lost_transitions=0,
                  duplicate_input_reports=0, unmatched_input_reports=0)
    input_counts = {stamp: 0 for stamp in samples}
    previous_state = None
    seen, latencies = set(), []
    previous_sequence, segment_start = None, None
    for report in reports:
        sequence, flags = int(report['report_sequence']), int(report['flags'])
        if previous_sequence is not None:
            delta = (sequence - previous_sequence) & 0xffffffff
            if delta >= 0x80000000:
                result['sequence_discontinuities'] += 1
            elif delta > 0:
                result['sequence_gaps'] += delta - 1
        previous_sequence = sequence
        code, state = int(report['result']), int(report['state'])
        result['gate_rejected'] += code == TrackReportResult.GATE_REJECTED
        result['time_rejected'] += code == TrackReportResult.TIME_REJECTED
        result['track_starts'] += code == TrackReportResult.STARTED
        result['lost_transitions'] += state == TrackState.LOST and previous_state != TrackState.LOST
        previous_state = state
        input_stamp = int(report['input_timestamp_ms'])
        sample = samples.get(input_stamp) if flags & 1 else None
        if flags & 1:
            if sample is None:
                result['unmatched_input_reports'] += 1
            else:
                result['duplicate_input_reports'] += input_counts[input_stamp] > 0
                input_counts[input_stamp] += 1
        if sample is not None:
            latency = (float(report['rx_monotonic_s']) - float(sample['tx_monotonic_s'])) * 1000
            if latency >= 0:
                latencies.append(latency)
        if not flags & 2:
            result['invalid_estimates'] += 1
            continue
        stamp = int(report['estimate_timestamp_ms'])
        if int(report['result']) == TrackReportResult.STARTED or segment_start is None:
            segment_start = stamp
        sample = samples.get(stamp)
        if sample is None:
            result['unmatched_estimates'] += 1
            continue
        if stamp in seen:
            result['duplicate_estimate_times'] += 1
            continue
        seen.add(stamp)
        if stamp - segment_start < warmup_ms:
            result['warmup_excluded'] += 1
            continue
        truth_range, truth_angle = float(sample['truth_range_m']), float(sample['truth_azimuth_rad'])
        errors['range_measurement_rmse_m'].append(float(sample['measured_range_m']) - truth_range)
        errors['range_estimate_rmse_m'].append(float(report['range_m']) - truth_range)
        for key, angle in [('azimuth_measurement_rmse_rad', float(sample['measured_azimuth_rad'])),
                           ('azimuth_estimate_rmse_rad', float(report['azimuth_rad']))]:
            errors[key].append((angle - truth_angle + math.pi) % (2 * math.pi) - math.pi)
        result['matched_estimates'] += 1
    result['missing_input_reports'] = sum(count == 0 for count in input_counts.values())
    result['matched_input_reports'] = sum(count > 0 for count in input_counts.values())
    result['estimate_coverage'] = result['matched_estimates'] / len(samples) if samples else None
    result['gate_rejection_fraction'] = result['gate_rejected'] / len(samples) if samples else None
    for key, values in errors.items():
        result[key.replace('_rmse_', '_max_abs_error_')] = max(map(abs, values)) if values else None
        result[key] = math.sqrt(sum(value * value for value in values) / len(values)) if values else None
    latencies.sort()
    result['pc_roundtrip_ms'] = {'count': len(latencies)}
    for name, fraction in [('p50', .5), ('p95', .95), ('p99', .99), ('max', 1)]:
        result['pc_roundtrip_ms'][name] = latencies[max(0, math.ceil(fraction * len(latencies)) - 1)] if latencies else None
    if 'spike' in metadata:
        from .outliers import analyze_spike
        result['spike'] = analyze_spike(sample_rows, reports, metadata['spike'])
    return result
