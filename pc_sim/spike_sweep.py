"""Run stationary-target UART spike/recovery cases; no firmware retuning."""
import argparse
from dataclasses import asdict
import json
from pathlib import Path

from .evaluate import run_scenario, scenario_settings, scenario_samples
from .outliers import SpikeSpec, inject_spike
from .protocol import TrackReportResult
from .recording import session_path

# Offsets around the default 100 m / 10 degree gates, plus wire-domain extremes.
RANGE_OFFSETS = (-50, 50, -99, 99, -100, 100, -101, 101, -500, 500, -999, 64535)
ANGLE_OFFSETS = (-5, 5, -9.9, 9.9, -10, 10, -10.1, 10.1, -45, 45, -180, 180)


def make_plan(count=120, hz=10, start_index=40, lengths=(1, 5),
              range_offsets=RANGE_OFFSETS, angle_offsets=ANGLE_OFFSETS,
              recovery_range_m=2, recovery_azimuth_deg=.2, recovery_hold=3):
    settings = scenario_settings('spike', count, hz)
    samples = scenario_samples(settings, count)
    if not lengths or not range_offsets or not angle_offsets:
        raise ValueError('Lengths and offset lists must not be empty')
    cases = [('control', 0, 0, 1)]
    for length in dict.fromkeys(lengths):
        cases.extend(('range', offset, 0, length) for offset in dict.fromkeys(range_offsets))
        cases.extend(('azimuth', 0, offset, length) for offset in dict.fromkeys(angle_offsets))
    plan = []
    for axis, distance, angle, length in cases:
        spec = SpikeSpec(start_index, length, distance, angle,
                         recovery_range_m, recovery_azimuth_deg, recovery_hold)
        spec.validate(count, hz, 2000)
        inject_spike(samples, spec)  # Preflight: no clipping, files, or port side effects.
        # Observe exact nominal gate boundaries; wire/float rounding matters there.
        expected = (None if abs(distance) == 100 or abs(angle) == 10 else
                    int(TrackReportResult.GATE_REJECTED) if abs(distance) > 100 or abs(angle) > 10 else
                    int(TrackReportResult.UPDATED))
        plan.append({'axis': axis, 'count': count, 'hz': hz, 'spike': asdict(spec),
                     'expected_first_result': expected})
    return plan


def run_suite(port, output, plan):
    if not port or not plan:
        raise ValueError('A port and a nonempty plan are required')
    path = session_path(output)
    path.mkdir(parents=True, exist_ok=False)
    document = {'schema_version': 1, 'mode': 'board', 'complete': False,
                'reference_gates': {'range_m': 100, 'azimuth_deg': 10,
                                    'note': 'Expected default firmware settings; not read back from the board'},
                'plan': plan, 'runs': []}
    def save():
        document['passed_runs'] = sum(r.get('passed', False) for r in document['runs'])
        document['failed_runs'] = len(document['runs']) - document['passed_runs']
        (path / 'spikes.json').write_text(json.dumps(document, indent=2, allow_nan=False) + '\n')
    save()
    try:
        for index, case in enumerate(plan):
            entry = {'case': case, 'passed': False}
            try:
                folder, result = run_scenario(port, 'spike', path, case['count'], case['hz'],
                                              spike=SpikeSpec(**case['spike']))
                entry.update(session=str(folder.relative_to(path)), result=result)
                errors = ('missing_input_reports', 'duplicate_input_reports', 'unmatched_input_reports',
                          'sequence_gaps', 'sequence_discontinuities', 'time_rejected')
                entry['transport_ok'] = not any(result[k] for k in errors) and result['recording_closed']
                expected = case['expected_first_result']
                entry['first_gate_ok'] = expected is None or result['spike']['first_spike_result'] == expected
                entry['passed'] = (entry['transport_ok'] and entry['first_gate_ok'] and result['spike']['pre_spike_settled']
                                   and result['spike']['recovered'])
                if not entry['passed']:
                    entry['error'] = 'Response integrity, pre-spike settling, first gate decision, or recovery criterion failed'
            except (ValueError, OSError, TimeoutError) as error:
                entry['error'] = str(error)
                document['runs'].append(entry)
                save()
                raise
            document['runs'].append(entry)
            save()
            print(f"{index + 1}/{len(plan)} {case['axis']} length={case['spike']['length']} "
                  f"passed={entry['passed']} session={folder}", flush=True)
        document['complete'] = True
    finally:
        save()
    return path, document


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port')
    parser.add_argument('--output', type=Path, default=Path('runs'))
    parser.add_argument('--count', type=int, default=120)
    parser.add_argument('--hz', type=float, default=10)
    parser.add_argument('--start-index', type=int, default=40, help='Zero-based sample index')
    parser.add_argument('--lengths', type=int, nargs='+', default=(1, 5))
    parser.add_argument('--range-offsets', type=float, nargs='+', default=RANGE_OFFSETS)
    parser.add_argument('--angle-offsets-deg', type=float, nargs='+', default=ANGLE_OFFSETS)
    parser.add_argument('--recovery-range-m', type=float, default=2)
    parser.add_argument('--recovery-angle-deg', type=float, default=.2)
    parser.add_argument('--recovery-hold', type=int, default=3)
    parser.add_argument('--dry-run', action='store_true')
    args = parser.parse_args(argv)
    try:
        plan = make_plan(args.count, args.hz, args.start_index, args.lengths,
                         args.range_offsets, args.angle_offsets_deg,
                         args.recovery_range_m, args.recovery_angle_deg, args.recovery_hold)
        if args.dry_run:
            print(json.dumps({'run_count': len(plan), 'plan': plan}, indent=2, allow_nan=False))
            return
        if not args.port:
            parser.error('--port is required unless --dry-run is used')
        path, document = run_suite(args.port, args.output, plan)
        print(f'Spike results: {path / "spikes.json"}')
        if document['failed_runs']:
            parser.exit(1, 'One or more spike/recovery cases failed; inspect spikes.json\n')
    except (ValueError, OSError, TimeoutError) as error:
        parser.exit(1, f'{error}\n')


if __name__ == '__main__':
    main()
