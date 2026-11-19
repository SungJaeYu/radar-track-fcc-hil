"""Deterministic measurement-only spikes and recovery evaluation from raw reports."""
from dataclasses import dataclass, replace
import math
import struct

from .protocol import build_measurement_frame_from_radians, TrackState, TrackReportResult


@dataclass(frozen=True)
class SpikeSpec:
    start_index: int = 40
    length: int = 1
    range_offset_m: float = 0
    azimuth_offset_deg: float = 0
    recovery_range_m: float = 2
    recovery_azimuth_deg: float = .2
    recovery_hold: int = 3

    def validate(self, count, hz, warmup_ms):
        for name in ('start_index', 'length', 'recovery_hold'):
            if type(getattr(self, name)) is not int:
                raise ValueError(f'{name} must be an integer')
        if self.start_index < 3 or self.length < 1 or self.recovery_hold < 1:
            raise ValueError('Spike requires >=3 preceding samples, length >=1 and recovery_hold >=1')
        if self.start_index + self.length + self.recovery_hold > count:
            raise ValueError('Spike must leave enough clean samples to confirm recovery')
        if round(self.start_index * 1000 / hz) < warmup_ms:
            raise ValueError('Spike must start at or after warmup')
        values = (self.range_offset_m, self.azimuth_offset_deg,
                  self.recovery_range_m, self.recovery_azimuth_deg)
        if not all(math.isfinite(x) for x in values):
            raise ValueError('Spike offsets and recovery tolerances must be finite')
        if self.recovery_range_m < 0 or not 0 <= self.recovery_azimuth_deg <= 180:
            raise ValueError('Recovery tolerances require range >=0 m and angle in 0..180 degrees')
        if not -180 <= self.azimuth_offset_deg <= 180:
            raise ValueError('Angular spike must be in -180..180 degrees')


def inject_spike(samples, spec):
    """Change the measurement wire values only; ground truth and time stay intact."""
    result = list(samples)
    for index in range(spec.start_index, spec.start_index + spec.length):
        sample = samples[index]
        frame = build_measurement_frame_from_radians(
            sample.timestamp_ms, sample.measured_range_m + spec.range_offset_m,
            sample.azimuth_rad + math.radians(spec.azimuth_offset_deg))
        _, distance, angle = struct.unpack('>IHi', frame[3:-2])
        result[index] = replace(sample, measured_range_m=distance,
                                azimuth_rad=angle * math.pi / 2**31, frame=frame)
    return result


def analyze_spike(sample_rows, report_rows, specification):
    """Use every post-spike sample, including reacquisition warmup and invalid reports.

    Recovery requires consecutive, current-time, CONFIRMED estimates within both
    declared error bounds. Missing/duplicate responses break the streak. Times
    below are sensor times, not wall-clock or MCU processing latency.
    """
    spec = SpikeSpec(**specification)
    samples = sorted(sample_rows, key=lambda row: int(row['timestamp_ms']))
    first_clean = spec.start_index + spec.length
    if len(samples) < first_clean + spec.recovery_hold:
        raise ValueError('Incomplete spike recording: not enough samples for recovery evaluation')
    responses = {}
    for row in report_rows:
        if int(row['flags']) & 1:
            responses.setdefault(int(row['input_timestamp_ms']), []).append(row)

    def current_report(sample):
        stamp = int(sample['timestamp_ms'])
        matched = responses.get(stamp, [])
        return matched[0] if len(matched) == 1 else None

    def errors(sample, report):
        if (report is None or not int(report['flags']) & 2 or
                int(report['estimate_timestamp_ms']) != int(sample['timestamp_ms'])):
            return None
        distance = float(report['range_m']) - float(sample['truth_range_m'])
        angle = float(report['azimuth_rad']) - float(sample['truth_azimuth_rad'])
        angle = (angle + math.pi) % (2 * math.pi) - math.pi
        if not math.isfinite(distance) or not math.isfinite(angle):
            return None
        return abs(distance), abs(math.degrees(angle))

    def settled(sample):
        report = current_report(sample)
        error = errors(sample, report)
        return (error is not None and int(report['state']) == TrackState.CONFIRMED
                and int(report['result']) == TrackReportResult.UPDATED
                and error[0] <= spec.recovery_range_m and error[1] <= spec.recovery_azimuth_deg)

    injected = samples[spec.start_index:first_clean]
    precondition = all(settled(s) for s in samples[spec.start_index - 3:spec.start_index])
    invalid, distances, angles = 0, [], []
    for sample in samples[spec.start_index:]:
        error = errors(sample, current_report(sample))
        if error is None:
            invalid += 1
        else:
            distances.append(error[0])
            angles.append(error[1])
    outcome = {
        'pre_spike_settled': precondition,
        'injected_samples': len(injected),
        'first_spike_result': (int(current_report(injected[0])['result'])
                               if current_report(injected[0]) is not None else None),
        'spike_start_sensor_ms': int(injected[0]['timestamp_ms']),
        'last_spike_sensor_ms': int(injected[-1]['timestamp_ms']),
        'first_clean_sensor_ms': int(samples[first_clean]['timestamp_ms']),
        'recovered': False, 'first_recovered_sensor_ms': None,
        'recovery_delay_sensor_ms': None, 'clean_samples_to_recovery': None,
        'confirmation_sensor_ms': None,
        'recovery_range_m': spec.recovery_range_m,
        'recovery_azimuth_deg': spec.recovery_azimuth_deg,
        'recovery_hold': spec.recovery_hold,
        'peak_range_error_m': max(distances) if distances else None,
        'peak_azimuth_error_deg': max(angles) if angles else None,
        'unavailable_estimates_during_or_after_spike': invalid,
        'injected_gate_rejections': sum(
            current_report(s) is not None and
            int(current_report(s)['result']) == TrackReportResult.GATE_REJECTED for s in injected),
    }
    streak = 0
    for index in range(first_clean, len(samples)):
        streak = streak + 1 if settled(samples[index]) else 0
        if streak == spec.recovery_hold:
            recovered_index = index - spec.recovery_hold + 1
            recovered_ms = int(samples[recovered_index]['timestamp_ms'])
            outcome.update(recovered=True, first_recovered_sensor_ms=recovered_ms,
                           recovery_delay_sensor_ms=recovered_ms - outcome['first_clean_sensor_ms'],
                           clean_samples_to_recovery=recovered_index - first_clean + 1,
                           confirmation_sensor_ms=int(samples[index]['timestamp_ms']))
            break
    return outcome
