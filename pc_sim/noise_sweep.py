"""Repeat real-board evaluations across independent noise levels and random seeds."""
import argparse
from dataclasses import asdict
import json
import math
from pathlib import Path
from statistics import mean

from .evaluate import run_scenario, scenario_settings, scenario_samples
from .recording import session_path

RANGE_LEVELS = (0, 2, 5, 8, 15, 30)
ANGLE_LEVELS = (0, 0.1, 0.5, 1, 3)


def make_plan(range_levels=RANGE_LEVELS, angle_levels=ANGLE_LEVELS, seeds=tuple(range(10)),
              scenario='noisy', count=120, hz=10, warmup_ms=2000):
    """Vary one axis at a time; deduplicate the shared zero-noise baseline."""
    if not range_levels or not angle_levels or not seeds:
        raise ValueError('Noise levels and seeds must not be empty')
    if any(not math.isfinite(x) or x < 0 for x in (*range_levels, *angle_levels)):
        raise ValueError('Noise levels must be finite and nonnegative')
    if any(type(seed) is not int for seed in seeds):
        raise ValueError('Seeds must be integers')
    pairs = dict.fromkeys([(x, 0) for x in range_levels] + [(0, x) for x in angle_levels])
    plan = []
    for distance, angle in pairs:
        for seed in dict.fromkeys(seeds):
            settings = scenario_settings(scenario, count, hz, warmup_ms, distance, angle, seed)
            # Reject an unrepresentable generated sample before any run opens a port.
            scenario_samples(settings, count)
            plan.append({'scenario': scenario, 'count': count, 'hz': hz, 'warmup_ms': warmup_ms,
                         'range_noise_std_m': distance, 'azimuth_noise_std_deg': angle,
                         'seed': seed, 'settings': asdict(settings)})
    return plan


def summarize_runs(runs):
    """Equal weight per seed; keep failed-run counts next to the metric summaries."""
    groups = {}
    metrics = ('range_measurement_rmse_m', 'range_estimate_rmse_m',
               'azimuth_measurement_rmse_rad', 'azimuth_estimate_rmse_rad',
               'range_estimate_max_abs_error_m', 'azimuth_estimate_max_abs_error_rad',
               'estimate_coverage', 'gate_rejection_fraction', 'track_starts', 'lost_transitions')
    for run in runs:
        case = run['case']
        key = (case['range_noise_std_m'], case['azimuth_noise_std_deg'])
        groups.setdefault(key, []).append(run)
    result = []
    for (distance, angle), group in groups.items():
        valid = [run['result'] for run in group if run['status'] == 'recorded']
        row = {'range_noise_std_m': distance, 'azimuth_noise_std_deg': angle,
               'runs': len(group), 'recorded_runs': len(valid),
               'failed_runs': len(group) - len(valid), 'metrics': {}}
        for metric in metrics:
            values = [r[metric] for r in valid if r.get(metric) is not None]
            row['metrics'][metric] = ({'count': len(values), 'mean': mean(values),
                                       'min': min(values), 'max': max(values)} if values else None)
        result.append(row)
    return result


def run_sweep(port, output, plan):
    if not port or not plan:
        raise ValueError('A port and a nonempty plan are required')
    path = session_path(output)
    path.mkdir(parents=True, exist_ok=False)
    document = {'schema_version': 1, 'mode': 'board', 'complete': False, 'plan': plan, 'runs': []}
    def save():
        document['summary'] = summarize_runs(document['runs'])
        (path / 'sweep.json').write_text(json.dumps(document, indent=2, allow_nan=False) + '\n')
    save()
    try:
        for case in plan:
            entry = {'case': case, 'status': 'failed'}
            try:
                folder, result = run_scenario(port, output=path, **{
                    key: value for key, value in case.items() if key != 'settings'})
                entry.update(session=str(folder.relative_to(path)), result=result)
                bad = ('missing_input_reports', 'duplicate_input_reports', 'unmatched_input_reports',
                       'sequence_gaps', 'sequence_discontinuities', 'time_rejected')
                if any(result[key] for key in bad) or not result['recording_closed']:
                    entry['error'] = 'Response correspondence/sequence/timestamp check failed'
                else:
                    entry['status'] = 'recorded'
            except (ValueError, OSError, TimeoutError) as error:
                entry['error'] = str(error)
                # Preserve partial evidence and stop; do not repeat a broken UART 100 times.
                document['runs'].append(entry)
                save()
                raise
            document['runs'].append(entry)
            save()
        document['complete'] = True
    finally:
        save()
    return path, document


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port')
    parser.add_argument('--output', type=Path, default=Path('runs'))
    parser.add_argument('--range-noise', type=float, nargs='+', default=RANGE_LEVELS)
    parser.add_argument('--azimuth-noise-deg', type=float, nargs='+', default=ANGLE_LEVELS)
    parser.add_argument('--seeds', type=int, nargs='+', default=tuple(range(10)))
    parser.add_argument('--scenario', choices=('noisy', 'dropout'), default='noisy')
    parser.add_argument('--count', type=int, default=120)
    parser.add_argument('--hz', type=float, default=10)
    parser.add_argument('--warmup-ms', type=int, default=2000)
    parser.add_argument('--dry-run', action='store_true', help='Validate and print plan without UART or files')
    args = parser.parse_args(argv)
    try:
        plan = make_plan(args.range_noise, args.azimuth_noise_deg, args.seeds,
                         args.scenario, args.count, args.hz, args.warmup_ms)
        if args.dry_run:
            print(json.dumps({'run_count': len(plan), 'plan': plan}, indent=2, allow_nan=False))
            return
        if not args.port:
            parser.error('--port is required unless --dry-run is used')
        path, document = run_sweep(args.port, args.output, plan)
        print(f'Sweep: {path}')
        print(json.dumps(document['summary'], indent=2, allow_nan=False))
        if any(run['status'] != 'recorded' for run in document['runs']):
            parser.exit(1, 'Sweep completed with failed runs; inspect sweep.json\n')
    except (ValueError, OSError, TimeoutError) as error:
        parser.exit(1, f'{error}\n')


if __name__ == '__main__':
    main()
