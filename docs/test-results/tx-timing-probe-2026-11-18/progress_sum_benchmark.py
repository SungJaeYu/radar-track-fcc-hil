import csv,gzip,sys,time,statistics,json
from pathlib import Path
sys.path.insert(0,str(Path.cwd()))
from pc_sim.protocol import TrackReport,TrackState,TrackReportResult,TrackReportFlag
root=Path('docs/test-results/perf-load-2026-11-18-retry/session')
with gzip.open(root/'reports.csv.gz','rt') as stream:
 reports=[TrackReport(int(r['report_sequence']),int(r['input_timestamp_ms']),int(r['estimate_timestamp_ms']),float(r['range_m']),float(r['azimuth_rad']),float(r['range_rate_mps']),float(r['azimuth_rate_radps']),TrackState(int(r['state'])),TrackReportResult(int(r['result'])),TrackReportFlag(int(r['flags']))) for r in csv.DictReader(stream)]
result=[]
for n in [3000,30000,60000,89971]:
 subset=reports[:n];measurements=[]
 for _ in range(7):
  start=time.perf_counter_ns();count=sum(r.input_present for r in subset);measurements.append((time.perf_counter_ns()-start)/1e6)
 result.append({'reports':n,'count':count,'median_ms':statistics.median(measurements),'max_ms':max(measurements)})
print(json.dumps(result,indent=2))
Path('/private/tmp/radar_progress_bench.json').write_text(json.dumps(result,indent=2)+'\n')
