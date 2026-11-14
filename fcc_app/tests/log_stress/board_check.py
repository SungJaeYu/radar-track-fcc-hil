"""Flash a bench image, capture UARTs, and verify bounded log overload recovery."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import threading
import time

import serial

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from pc_sim.frame_parser import FrameParser, ParserState
from pc_sim.protocol import build_init_frame, build_radar_frame, decode_status_payload
from pc_sim.status_client import request_status


def check_wire(records, expected_responses):
    """Check every captured response, including malformed frames a client may skip."""
    parser = FrameParser()
    frames = []
    for record in records:
        if 'rx' in record:
            frames.extend(parser.feed(bytes.fromhex(record['rx'])))
    return (len(frames) == expected_responses > 0
            and parser.crc_errors == parser.length_errors == 0
            and parser.state == ParserState.WAIT_STX
            and all(frame.frame_type == 6 and len(frame.payload) == 28
                    and all(value == 0 for value in decode_status_payload(frame.payload).values())
                    for frame in frames))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mode', choices=('producer', 'traffic'), required=True)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--data-port', required=True)
    parser.add_argument('--console-port', required=True)
    parser.add_argument('--west', required=True)
    parser.add_argument('--zephyr-workspace', type=Path, required=True)
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=False)
    build = args.build_dir.resolve()
    config = (build / 'zephyr/.config').read_text()
    enabled = 'CONFIG_RADAR_LOG_STRESS_TEST=y' in config
    if enabled != (args.mode == 'producer'):
        raise ValueError('Build does not match requested test mode')
    if 'CONFIG_RADAR_HANDLER_LOG_LEVEL=4' not in config:
        raise ValueError('DEBUG measurement logs are required')
    (args.output_dir / 'firmware.config').write_text(config)
    results = {'mode': args.mode, 'phases': [], 'status': [], 'errors': [],
               'hex_sha256': hashlib.sha256((build / 'zephyr/zephyr.hex').read_bytes()).hexdigest()}
    origin = time.monotonic()
    stop = threading.Event()
    console_chunks = []
    traffic = []

    def capture(console):
        try:
            while not stop.is_set():
                data = console.read(4096)
                if data:
                    console_chunks.append((time.monotonic() - origin, data))
        except Exception as error:
            results['errors'].append(f'console: {error}')

    def status(port, phase):
        start = time.monotonic()
        try:
            stats = request_status(port, timeout=2.0)
            results['status'].append({'phase': phase, 'at_s': start - origin,
                                      'elapsed_ms': (time.monotonic() - start) * 1000,
                                      'stats': stats})
        except Exception as error:
            results['errors'].append(f'{phase} STATUS: {error}')

    class RecordedSerial(serial.Serial):
        def write(self, data):
            count = super().write(data)
            traffic.append({'at_s': time.monotonic() - origin, 'tx': data[:count].hex()})
            if count != len(data):
                raise OSError('Incomplete UART write')
            return count

        def read(self, size=1):
            data = super().read(size)
            if data:
                traffic.append({'at_s': time.monotonic() - origin, 'rx': data.hex()})
            return data

    try:
        with serial.Serial(args.console_port, 115200, timeout=0.05) as console:
            console.reset_input_buffer()
            collector = threading.Thread(target=capture, args=(console,))
            collector.start()
            try:
                # Flash in this process so the one-shot producer cannot run before capture.
                with (args.output_dir / 'flash.log').open('w') as log:
                    subprocess.run([args.west, 'flash', '-d', str(build), '--runner', 'openocd'],
                                   cwd=args.zephyr_workspace, stdout=log, stderr=subprocess.STDOUT,
                                   check=True, timeout=60)
                with RecordedSerial(args.data_port, 115200, timeout=0.05, write_timeout=2) as port:
                    port.reset_input_buffer()
                    port.write(build_init_frame())
                    status(port, 'baseline')
                    phases = [('baseline', 10, 3), ('loaded', 10, 12), ('recovery', 10, 3)]
                    if args.mode == 'traffic':
                        phases = [('baseline', 10, 3), ('50Hz', 50, 3), ('100Hz', 100, 3),
                                  ('200Hz', 200, 3), ('recovery', 10, 3)]
                    measurement_origin = time.monotonic()
                    for name, rate, duration in phases:
                        start = time.monotonic()
                        next_measurement = next_status = start
                        sent = 0
                        while time.monotonic() - start < duration:
                            now = time.monotonic()
                            if now >= next_measurement:
                                stamp = int((now - measurement_origin) * 1000)
                                port.write(build_radar_frame(stamp, 1000, 0))
                                results['last_measurement_ms'] = stamp
                                sent += 1
                                next_measurement = now + 1 / rate
                            if now >= next_status:
                                status(port, name)
                                next_status = time.monotonic() + 0.25
                            time.sleep(0.0005)
                        results['phases'].append({'name': name, 'target_hz': rate,
                                                  'sent': sent, 'duration_s': time.monotonic() - start})
                        status(port, name + '-end')
                    time.sleep(3.5)
                    status(port, 'after-stop')
                    time.sleep(1.2)
            finally:
                stop.set()
                collector.join(timeout=2)
    except Exception as error:
        results['errors'].append(str(error))
    finally:
        raw = b''.join(data for _, data in console_chunks)
        (args.output_dir / 'console.log').write_bytes(raw)
        clean = re.sub(r'\x1b\[[0-9;]*m', '', raw.decode(errors='replace'))
        drops = [int(value) for value in re.findall(r'(\d+) messages dropped', clean)]
        results['log_dropped_notice_total'] = sum(drops)
        results['producer_sequences_received'] = len(set(re.findall(r'Log stress sequence=(\d+)', clean)))
        results['checks'] = {
            'no_io_errors': not results['errors'],
            'wire_responses_valid': check_wire(traffic, len(results['status'])),
            'log_loss_observed': sum(drops) > 0,
            'status_counters_zero': bool(results['status']) and all(
                all(value == 0 for value in sample['stats'].values()) for sample in results['status']),
            'status_each_phase': all(any(sample['phase'] == phase['name'] for sample in results['status'])
                                     for phase in results['phases']) and bool(results['phases']),
            'status_after_stop': any(s['phase'] == 'after-stop' for s in results['status']),
            'no_fatal_or_assert': not re.search(r'ASSERTION FAIL|FATAL ERROR|Hard fault', clean, re.I),
            'single_boot': clean.count('FCC learning-rebuild boot') == 1,
            'confirmed': 'TENTATIVE -> CONFIRMED' in clean,
            'recovery_measurement_logged': f"Measurement: t={results.get('last_measurement_ms')} ms" in clean,
            'timeout_once': clean.count('Radar measurement timeout: track cleared') == 1,
        }
        if args.mode == 'producer':
            results['checks']['producer_completed'] = 'Log stress complete: attempted=20000' in clean
            results['checks']['producer_started'] = 'Log stress starting:' in clean
        results['passed'] = all(results['checks'].values())
        (args.output_dir / 'traffic.json').write_text(json.dumps(traffic, indent=2) + '\n')
        (args.output_dir / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
        print(json.dumps({k: v for k, v in results.items() if k != 'status'}, indent=2))
    return 0 if results['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
