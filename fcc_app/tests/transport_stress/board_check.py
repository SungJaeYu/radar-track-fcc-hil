"""Force bounded consumer stalls on a bench image, then restore normal firmware."""
import argparse
from dataclasses import asdict
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import time

import serial

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from pc_sim.frame_parser import FrameParser
from pc_sim.protocol import (build_init_frame, build_measurement_frame, encode_frame,
                             decode_status_payload, decode_track_payload)
from pc_sim.status_client import request_status


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--data-port', required=True)
    parser.add_argument('--console-port', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--west', default='west')
    parser.add_argument('--workspace', type=Path, required=True)
    args = parser.parse_args()
    # Validate the restore image before touching the board.
    for build, enabled in [('build', False), ('build_transport_stress', True)]:
        config = (ROOT / build / 'zephyr/.config').read_text()
        if ('CONFIG_RADAR_TRANSPORT_STRESS_TEST=y' in config) != enabled:
            parser.error(f'Unexpected transport fixture configuration in {build}')
        if 'CONFIG_RADAR_LOG_STRESS_TEST=y' in config:
            parser.error(f'Log stress must be disabled in {build}')
        if not (ROOT / build / 'zephyr/zephyr.hex').is_file():
            parser.error(f'Missing firmware image in {build}')
    args.output.mkdir(parents=True, exist_ok=False)
    records, checks, reports, statuses = [], [], [], []
    console = ''
    decoder = FrameParser()

    def flash(build, name):
        with (args.output / name).open('w') as log:
            subprocess.run([args.west, 'flash', '-d', str(ROOT / build), '--runner', 'openocd'],
                           cwd=args.workspace, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=45)

    def check(name, condition):
        checks.append({'name': name, 'passed': bool(condition)})
        if not condition:
            raise AssertionError(name)

    failure = None
    try:
        flash('build_transport_stress', 'bench-flash.log')
        with serial.Serial(args.data_port, 115200, timeout=0, write_timeout=1) as data, \
             serial.Serial(args.console_port, 115200, timeout=0) as log:
            def send(raw):
                check('complete PC write', data.write(raw) == len(raw))
                records.append({'tx': raw.hex(), 'at': time.monotonic()})

            def pump(seconds):
                nonlocal console
                end = time.monotonic() + seconds
                while time.monotonic() < end:
                    console += log.read(min(log.in_waiting, 8192)).decode(errors='replace')
                    raw = data.read(min(data.in_waiting, 8192))
                    if raw:
                        records.append({'rx': raw.hex(), 'at': time.monotonic()})
                    for frame in decoder.feed(raw):
                        if frame.frame_type == 6:
                            statuses.append(decode_status_payload(frame.payload))
                        elif frame.frame_type == 7:
                            reports.append(decode_track_payload(frame.payload))
                        else:
                            raise AssertionError('Unexpected response TYPE')
                    time.sleep(.002)

            def marker(text, timeout=10):
                end = time.monotonic() + timeout
                while text not in console and time.monotonic() < end:
                    pump(.02)
                check(text, text in console)

            def status():
                count = len(statuses)
                send(encode_frame(3, b'\0'))
                pump(.15)
                check('STATUS response', len(statuses) == count + 1)
                return statuses[-1]

            def recover():
                start = len(reports)
                send(build_init_frame())
                pump(.05)
                for stamp in (0, 100, 200):
                    send(build_measurement_frame(stamp, 1000, 0))
                    pump(.05)
                check('fresh tracking CONFIRMED', len(reports) >= start + 4 and reports[-1].state == 2)
                send(encode_frame(5, b'\0'))
                pump(.05)

            before = status()
            marker('BENCH RX PAUSED')
            # Noise fills the ring without requiring a valid queued frame.
            send(bytes(600))
            marker('BENCH RX RESUMED', 7)
            after = status()
            check('real ring overflow counted', after['ring_overflow_events'] > before['ring_overflow_events'])
            check('ISR discarded bytes counted', after['rx_dropped_bytes'] > before['rx_dropped_bytes'])
            recover()
            before = status()
            marker('BENCH HANDLER PAUSED')
            # Pace below line capacity; isolate handler queue pressure from ring pressure.
            for index in range(24):
                send(build_measurement_frame(index * 100, 1000, 0))
                pump(.01)
            start = len(reports)
            marker('BENCH HANDLER RESUMED', 7)
            after = status()
            check('RX queue drops counted', after['queue_drops'] > before['queue_drops'])
            check('old queued measurements dropped', after['stale_measurements'] > before['stale_measurements'])
            check('stale backlog emits no TRACK', len(reports) == start)
            check('handler pressure did not overflow ring', after['ring_overflow_events'] == before['ring_overflow_events'])
            recover()
            marker('BENCH TX PAUSED')
            count = len(statuses)
            for _ in range(24):
                send(encode_frame(3, b'\0'))
                pump(.01)
            check('paused TX emits no response', len(statuses) == count)
            marker('BENCH TX RESUMED', 7)
            pump(.3)
            # A pending k_msgq_get may already own one item in addition to eight queued.
            accepted = len(statuses) - count
            check('TX preserves queue plus possible in-flight reply', 8 <= accepted <= 9)
            check('TX queue full diagnosed', 'STATUS submit failed:' in console and '(TX queue full)' in console)
            status()
            recover()
            marker('BENCH COMPLETE')
            check('received framing intact', decoder.crc_errors == decoder.length_errors == 0)
    except Exception as error:
        failure = repr(error)
    finally:
        (args.output / 'console.log').write_text(console)
        (args.output / 'wire.json').write_text(json.dumps(records, indent=2)+'\n')
        (args.output / 'reports.json').write_text(json.dumps([asdict(r) for r in reports], indent=2)+'\n')
        (args.output / 'status.json').write_text(json.dumps(statuses, indent=2)+'\n')
        try:
            flash('build', 'restore-flash.log')
            with serial.Serial(args.data_port, 115200, timeout=.05, write_timeout=1) as data:
                time.sleep(.3)
                restored = request_status(data, timeout=2)
                check('normal firmware STATUS restored', all(value == 0 for value in restored.values()))
        except Exception as error:
            failure = f'{failure or ""}; restore: {error!r}'
        result = {'checks': checks, 'failure': failure,
                  'hex_sha256': {name: hashlib.sha256((ROOT/name/'zephyr/zephyr.hex').read_bytes()).hexdigest()
                                 for name in ('build', 'build_transport_stress')}}
        (args.output / 'results.json').write_text(json.dumps(result, indent=2)+'\n')
    if failure:
        raise SystemExit(failure)
    print(f'PASS: {len(checks)} assertions; normal firmware restored')


if __name__ == '__main__':
    main()
