"""Independently replay a completed normal-load capture without serial access."""
import csv
import gzip
import json
import sys
from collections import Counter
from pathlib import Path

ROOT = Path.cwd()
sys.path.insert(0, str(ROOT))
from pc_sim.frame_parser import FrameParser, ParserState

source = Path(sys.argv[1])
def open_text(path):
    return path.open() if path.exists() else gzip.open(str(path) + '.gz', 'rt')
def rows(name):
    with open_text(source / 'session' / (name + '.csv')) as stream:
        return list(csv.DictReader(stream))

samples, reports, statuses, wire = [rows(n) for n in ('samples', 'reports', 'status', 'wire')]
measurements = [r for r in reports if int(r['flags']) & 1]
assert [int(r['timestamp_ms']) for r in samples] == [int(r['input_timestamp_ms']) for r in measurements]
assert [int(r['report_sequence']) for r in reports] == list(range(len(reports)))
assert all(int(r['state']) == (1 if i < 2 else 2) for i, r in enumerate(measurements))
assert len(reports) == len(samples) + 2
assert all(not int(r['flags']) and int(r['state']) == 0 and int(r['result']) == 4 for r in (reports[0], reports[-1]))
assert all(int(value) == 0 for r in statuses for key, value in r.items() if key != 'rx_monotonic_s')
decoders = {direction: FrameParser() for direction in ('tx', 'rx')}
counts = {direction: Counter() for direction in decoders}
byte_counts = Counter()
for chunk in wire:
    direction = chunk['direction']
    raw = bytes.fromhex(chunk['hex'])
    byte_counts[direction] += len(raw)
    for frame in decoders[direction].feed(raw):
        counts[direction][frame.frame_type] += 1
for parser in decoders.values():
    assert parser.crc_errors == parser.length_errors == 0
    assert parser.state == ParserState.WAIT_STX
assert counts['tx'] == {1: 1, 2: len(samples), 3: len(statuses)}
assert counts['rx'] == {7: len(reports), 6: len(statuses)}
result = {'passed': True, 'measurement_timestamps_match_in_order': len(samples),
          'contiguous_track_reports': len(reports), 'status_zero_responses': len(statuses),
          'wire_frames_by_type': counts, 'wire_bytes': byte_counts,
          'state_sequence': 'TENTATIVE for first two measurements, then CONFIRMED',
          'wire_crc_length_errors': 0, 'wire_partial_tails': 0}
with open_text(source / 'performance.json') as stream:
    summaries = json.load(stream)
with open_text(source / 'metadata.json') as stream:
    metadata = json.load(stream)
assert all(0 < b['header']['uptime_ms'] - a['header']['uptime_ms'] < 6000 for a, b in zip(summaries, summaries[1:]))
assert summaries[-1]['header']['measurements'] == len(samples)
sent_times = [float(r['tx_monotonic_s']) for r in samples]
sent = {int(r['timestamp_ms']): float(r['tx_monotonic_s']) for r in samples}
latencies = sorted((float(r['rx_monotonic_s']) - sent[int(r['input_timestamp_ms'])]) * 1000 for r in measurements)
gaps = [(b-a)*1000 for a,b in zip(sent_times,sent_times[1:])]
import math
result.update({
    'note': 'Wire/counter replay passed; this does not override the host pacing failure in results.json',
    'duration_target_s': metadata['duration_s'],
    'first_to_last_measurement_s': sent_times[-1]-sent_times[0],
    'actual_measurement_hz': len(samples)/metadata['duration_s'],
    'skipped_host_slots': metadata['duration_s']*50-len(samples),
    'host_max_send_gap_ms': max(gaps), 'host_gaps_over_100ms': sum(g>100 for g in gaps),
    'pc_latency_ms': {label:latencies[math.ceil(len(latencies)*q)-1] for label,q in [('p50',.5),('p95',.95),('p99',.99),('max',1)]},
    'performance_summary_count':len(summaries), 'summaries_continuous':True,
    'last_performance':summaries[-1],
})
(source / 'offline-audit.json').write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps(result, indent=2))
