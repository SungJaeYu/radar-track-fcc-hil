from pathlib import Path
import csv, json, re, shutil
root = Path('docs/test-results/tx-timing-probe-2026-11-18')
results = json.loads((root/'results.json').read_text())
profile = json.loads((root/'pc-operations.json').read_text())
samples = list(csv.DictReader((root/'session/samples.csv').open()))
start = float(samples[0]['tx_monotonic_s'])
end = start + 120
matches = re.findall(r'TXPROBE count=(\d+) queue_max_us=(\d+) poll_max_us=(\d+) poll_total_us=(\d+)', (root/'console.log').read_text())
count, queue_max, poll_max, total = map(int, matches[-1])
summary = {
    'source': '5550bcd plus temporary-firmware.patch; temporary profiling wrapper',
    'duration_s': 120,
    'tx_probe': {'count': count, 'queue_max_us': queue_max, 'poll_max_us': poll_max,
                 'poll_total_us': total, 'poll_mean_us': total/count,
                 'count_matches_received': count == results['track_reports'] + results['status_responses']},
    'slow_events_during_load': [dict(e, elapsed_s=e['at_monotonic_s']-start) for e in profile['slow_events'] if start <= e['at_monotonic_s'] < end],
    'limits': ['Short instrumented run does not replace the failed 30-minute acceptance test.',
               'Polling duration includes preemption; loop return does not mean last bit transmitted or peer received.',
               'PC per-operation profile covers serial, CSV rows and flush, sleep, sum, print and GC, not every Python operation or OS scheduling point.']
}
(root/'analysis.json').write_text(json.dumps(summary, indent=2)+'\n')
shutil.copyfile('/private/tmp/radar_progress_bench.py', root/'progress_sum_benchmark.py')
shutil.copyfile(__file__, root/'analyze_probe.py')
metadata = json.loads((root/'metadata.json').read_text())
metadata['source_note'] = summary['source']
(root/'metadata.json').write_text(json.dumps(metadata, indent=2)+'\n')
print(json.dumps(summary, indent=2))
print(json.dumps(results, indent=2))
print(json.dumps(profile['stats'], indent=2))
