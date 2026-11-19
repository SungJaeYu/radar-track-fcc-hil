"""Record fixed single-target HIL scenarios or evaluate existing CSV sessions."""
import argparse
from dataclasses import asdict
import json
import math
import random
import time
from pathlib import Path

import serial

from .frame_parser import FrameParser
from .outliers import inject_spike
from .gui_model import Settings, make_sample
from .protocol import (build_init_frame, encode_frame, decode_track_payload,
                       decode_status_payload, FRAME_TYPE_TRACK_REPORT, FRAME_TYPE_STATUS_REPORT)
from .recording import SessionRecorder, evaluate_session, session_path


def scenario_settings(scenario, count=120, hz=10, warmup_ms=2000,
                      range_noise_std_m=None, azimuth_noise_std_deg=0, seed=42):
    """Validate the complete run before creating files or opening a UART."""
    if scenario not in ('normal', 'noisy', 'dropout', 'spike') or type(count) is not int or count < 3:
        raise ValueError('scenario must be normal/noisy/dropout/spike; integer count >= 3')
    if not math.isfinite(hz) or not 1 <= hz <= 50:
        raise ValueError('hz must be finite and in 1..50')
    if type(warmup_ms) is not int or warmup_ms < 0:
        raise ValueError('warmup_ms must be a nonnegative integer')
    if warmup_ms > round((count - 1) * 1000 / hz):
        raise ValueError('warmup_ms must leave at least one sample for evaluation')
    if scenario == 'dropout' and (count / hz < 9 or hz < 5):
        raise ValueError('dropout requires at least 9 seconds at 5 Hz or more')
    noise = (0 if scenario in ('normal', 'spike') else 8) if range_noise_std_m is None else range_noise_std_m
    settings = Settings(range_rate_mps=0 if scenario == 'spike' else -5,
                        azimuth_deg=0 if scenario == 'spike' else 15, range_noise_std_m=noise,
                        azimuth_noise_std_deg=azimuth_noise_std_deg,
                        duration_s=count / hz, sample_rate_hz=hz, seed=seed)
    settings.validate()
    return settings


def scenario_samples(settings, count):
    """Preflight every deterministic measurement; never clip an invalid range."""
    rng = random.Random(settings.seed)
    return [make_sample(settings, round(index * 1000 / settings.sample_rate_hz), rng)
            for index in range(count)]


def run_scenario(port, scenario, output, count=120, hz=10, warmup_ms=2000, *,
                 range_noise_std_m=None, azimuth_noise_std_deg=0, seed=42, spike=None):
    settings = scenario_settings(scenario, count, hz, warmup_ms,
                                 range_noise_std_m, azimuth_noise_std_deg, seed)
    if (scenario == 'spike') != (spike is not None):
        raise ValueError('Provide SpikeSpec exactly when scenario is spike')
    if spike is not None:
        spike.validate(count, hz, warmup_ms)
    samples = scenario_samples(settings, count)
    if spike is not None:
        samples = inject_spike(samples, spike)
    path = session_path(output)
    parser = FrameParser()
    init_seen = False
    with SessionRecorder(path, settings, 'board') as recorder:
        recorder.metadata['scenario'] = scenario
        if spike is not None:
            recorder.metadata['spike'] = asdict(spike)
        recorder.metadata['dropout_sensor_interval_ms'] = [4000, 5500] if scenario == 'dropout' else None
        recorder._metadata()
        with serial.Serial(port, 115200, timeout=0.01, write_timeout=1) as data:
            def send(frame):
                now = time.monotonic()
                if data.write(frame) != len(frame):
                    raise OSError('Short UART write')
                recorder.record_wire('tx', frame, now)
                return now

            def collect(until):
                nonlocal init_seen
                while time.monotonic() < until:
                    raw = data.read(min(data.in_waiting or 1, 4096))
                    received_at = time.monotonic()
                    recorder.record_wire('rx', raw, received_at)
                    for frame in parser.feed(raw):
                        if frame.frame_type == FRAME_TYPE_TRACK_REPORT:
                            report = decode_track_payload(frame.payload)
                            recorder.record_report(report, received_at)
                            if not report.input_present and report.result == 4 and report.state == 0:
                                init_seen = True
                        elif frame.frame_type == FRAME_TYPE_STATUS_REPORT:
                            recorder.record_status(decode_status_payload(frame.payload), received_at)
                        else:
                            raise ValueError(f'Unexpected received TYPE {frame.frame_type}')
            send(build_init_frame())
            collect(time.monotonic() + .3)
            if not init_seen:
                raise TimeoutError('No INIT reset report; check firmware and UART wiring')
            send(encode_frame(3, b'\0'))
            collect(time.monotonic() + .15)
            # Samples include omitted slots to keep the seeded noise comparable.
            for sample in samples:
                stamp = sample.timestamp_ms
                if not (scenario == 'dropout' and 4000 <= stamp < 5500):
                    sent_at = send(sample.frame)
                    recorder.record_sample(sample, sent_at)
                # Deliberately no catch-up bursts; sensor time is the fixed scenario axis.
                collect(time.monotonic() + 1 / hz)
            collect(time.monotonic() + 1.2)
            send(encode_frame(3, b'\0'))
            collect(time.monotonic() + .15)
            send(encode_frame(5, b'\0'))
            collect(time.monotonic() + .15)
            if parser.crc_errors or parser.length_errors:
                raise ValueError('PC receive CRC/LEN errors; inspect wire.csv')
    result = evaluate_session(path, warmup_ms=warmup_ms)
    result['scenario'] = scenario
    (path / 'evaluation.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    if not result['matched_estimates']:
        raise ValueError(f'No matched post-warmup estimates: {path}')
    return path, result


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    run = sub.add_parser('run', help='Send a fixed scenario to the real board and save CSVs')
    run.add_argument('--port', required=True)
    run.add_argument('--scenario', choices=('normal', 'noisy', 'dropout'), default='noisy')
    run.add_argument('--output', type=Path, default=Path('runs'))
    run.add_argument('--count', type=int, default=120)
    run.add_argument('--hz', type=float, default=10)
    run.add_argument('--warmup-ms', type=int, default=2000)
    run.add_argument('--range-noise', type=float, default=None, help='Range noise standard deviation (m)')
    run.add_argument('--azimuth-noise-deg', type=float, default=0, help='Angular noise standard deviation (degrees)')
    run.add_argument('--seed', type=int, default=42)
    report = sub.add_parser('report', help='Evaluate a recorded board session without serial I/O')
    report.add_argument('directory', type=Path)
    report.add_argument('--warmup-ms', type=int, default=2000)
    args = parser.parse_args(argv)
    try:
        if args.command == 'run':
            path, result = run_scenario(args.port, args.scenario, args.output, args.count, args.hz, args.warmup_ms,
                                        range_noise_std_m=args.range_noise,
                                        azimuth_noise_std_deg=args.azimuth_noise_deg, seed=args.seed)
            print(f'Session: {path}')
        else:
            result = evaluate_session(args.directory, args.warmup_ms)
        print(json.dumps(result, indent=2))
    except (ValueError, OSError, TimeoutError) as error:
        parser.exit(1, f'{error}\n')


if __name__ == '__main__':
    main()
