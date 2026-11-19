"""Replay saved evaluation sessions and wire frames; no serial access."""
import csv,json,sys,math
from pathlib import Path
from collections import Counter
sys.path.insert(0,str(Path.cwd()))
from pc_sim.recording import evaluate_session
from pc_sim.frame_parser import FrameParser,ParserState
root=Path(sys.argv[1]);summary={'sessions':0,'samples':0,'track_reports':0,'status_reports':0,'wire_frames':{'tx':Counter(),'rx':Counter()},'matched_input_reports':0,'gate_rejections':0,'lost_transitions':0,'sequence_errors':0,'runs':[]}
for mode,filename in [('spike','spikes.json'),('noise','sweep.json')]:
 source=next((root/mode).glob('*/'+filename));doc=json.loads(source.read_text());assert doc['complete']
 for run in doc['runs']:
  folder=source.parent/run['session'];stored=json.loads((folder/'evaluation.json').read_text());warmup=run['case'].get('warmup_ms',2000)
  result=evaluate_session(folder,warmup_ms=warmup)
  for key,value in result.items():assert stored[key]==value,(folder,key,stored[key],value)
  for key in ('missing_input_reports','duplicate_input_reports','unmatched_input_reports','sequence_gaps','sequence_discontinuities','time_rejected'):assert result[key]==0,(folder,key)
  def rows(name):
   with (folder/(name+'.csv')).open() as stream:return list(csv.DictReader(stream))
  samples,reports,status=rows('samples'),rows('reports'),rows('status')
  assert len(samples)==run['case']['count']==120
  assert len(status)==2
  assert all(not int(v) for r in status for k,v in r.items() if k!='rx_monotonic_s'),folder
  decoders={d:FrameParser() for d in ('tx','rx')};counts={d:Counter() for d in decoders}
  for r in rows('wire'):
   for frame in decoders[r['direction']].feed(bytes.fromhex(r['hex'])):counts[r['direction']][frame.frame_type]+=1
  assert counts['tx']=={1:1,2:len(samples),3:2,5:1},folder
  assert counts['rx']=={7:len(reports),6:2},folder
  assert len(reports)==len(samples)+3,folder
  for parser in decoders.values():assert parser.crc_errors==parser.length_errors==0 and parser.state==ParserState.WAIT_STX
  for d in counts:summary['wire_frames'][d].update(counts[d])
  summary['sessions']+=1;summary['samples']+=len(samples);summary['track_reports']+=len(reports);summary['status_reports']+=len(status)
  summary['matched_input_reports']+=result['matched_input_reports'];summary['gate_rejections']+=result['gate_rejected'];summary['lost_transitions']+=result['lost_transitions']
  summary['runs'].append({'session':str(folder.relative_to(root)),'mode':mode,'actual_interval_hz':(len(samples)-1)/(float(samples[-1]['tx_monotonic_s'])-float(samples[0]['tx_monotonic_s'])),'pc_rtt_max_ms':result['pc_roundtrip_ms']['max']})
assert summary['sessions']==149
summary['passed']=True
summary['note']='Recorded evaluation metrics replay identically; wire/CSV correspondence and zero STATUS checked independently. Noise metrics are observations, not a predefined accuracy acceptance gate.'
(root/'offline-audit.json').write_text(json.dumps(summary,indent=2)+'\n')
print(json.dumps({k:v for k,v in summary.items() if k!='runs'},indent=2))
