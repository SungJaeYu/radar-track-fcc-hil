"""Capture current default spike/noise suites without changing production behavior."""
from pathlib import Path
import datetime, hashlib, json, re, subprocess, sys, threading, traceback
ROOT=Path.cwd()
sys.path.insert(0,str(ROOT))
import serial
from pc_sim import noise_sweep, spike_sweep
from pc_sim.status_client import request_status
OUTPUT=ROOT/'docs/test-results/post-pull-sweeps-2026-11-19'
spike_plan=spike_sweep.make_plan()
noise_plan=noise_sweep.make_plan()
config=(ROOT/'build/zephyr/.config').read_text()
for option in ['CONFIG_RADAR_RANGE_GATE_M=100','CONFIG_RADAR_AZIMUTH_GATE_DEG=10','CONFIG_RADAR_RX_OVERFLOW_DRAIN=y']:
 assert option in config,option
assert 'CONFIG_RADAR_PERF_MONITOR=y' not in config
OUTPUT.mkdir(parents=True,exist_ok=False)
git='/Applications/Xcode.app/Contents/Developer/usr/bin/git'
metadata={'started_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),
 'source_commit':subprocess.check_output([git,'rev-parse','HEAD'],text=True).strip(),
 'normal_elf_sha256':hashlib.sha256((ROOT/'build/zephyr/zephyr.elf').read_bytes()).hexdigest(),
 'data_port':'/dev/cu.usbserial-A5069RR4','console_port':'/dev/cu.usbmodem1203',
 'spike_cases':len(spike_plan),'noise_cases':len(noise_plan),
 'mode':'Existing run_suite/run_sweep, default plans; progress wrapper only for noise',
 'source_files_sha256':{str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in [Path('pc_sim/noise_sweep.py'),Path('pc_sim/spike_sweep.py'),Path('pc_sim/evaluate.py'),Path('pc_sim/outliers.py'),Path('pc_sim/recording.py')]}}
(OUTPUT/'metadata.json').write_text(json.dumps(metadata,indent=2)+'\n')
(OUTPUT/'build.config').write_text(config)
(OUTPUT/'board_capture.py').write_text(Path(__file__).read_text())
stop=threading.Event();console_errors=[]
def console_reader(port):
 try:
  with (OUTPUT/'console.log').open('wb') as stream:
   while not stop.is_set():
    data=port.read(min(port.in_waiting or 1,16384))
    if data:stream.write(data);stream.flush()
 except BaseException as error:
  console_errors.append(repr(error))
original=noise_sweep.run_scenario
noise_index=0
def noisy_progress(*args,**kwargs):
 global noise_index
 output=original(*args,**kwargs);noise_index+=1
 print(f'NOISE {noise_index}/{len(noise_plan)} range={kwargs.get("range_noise_std_m")} angle={kwargs.get("azimuth_noise_std_deg")} seed={kwargs.get("seed")}',flush=True)
 return output
noise_sweep.run_scenario=noisy_progress
result={}
try:
 with serial.Serial(metadata['console_port'],115200,timeout=.1) as console:
  console.reset_input_buffer();reader=threading.Thread(target=console_reader,args=(console,),daemon=True);reader.start()
  try:
   with serial.Serial(metadata['data_port'],115200,timeout=.05,write_timeout=1) as port:
    result['initial_status']=request_status(port)
   assert not any(result['initial_status'].values())
   spikes,spike_result=spike_sweep.run_suite(metadata['data_port'],OUTPUT/'spike',spike_plan)
   result['spike']={'path':str(spikes.relative_to(OUTPUT)),'complete':spike_result['complete'],'passed_runs':spike_result['passed_runs'],'failed_runs':spike_result['failed_runs']}
   print('SPIKE COMPLETE',result['spike'],flush=True)
   noise,noise_result=noise_sweep.run_sweep(metadata['data_port'],OUTPUT/'noise',noise_plan)
   result['noise']={'path':str(noise.relative_to(OUTPUT)),'complete':noise_result['complete'],'recorded_runs':sum(r['status']=='recorded' for r in noise_result['runs']),'failed_runs':sum(r['status']!='recorded' for r in noise_result['runs'])}
  finally:
   try:
    with serial.Serial(metadata['data_port'],115200,timeout=.05,write_timeout=1) as port:result['final_status']=request_status(port)
   except BaseException as error:result['final_status_error']=repr(error)
   stop.set();reader.join(2)
except BaseException as error:
 result['failure']=repr(error);traceback.print_exc()
finally:
 result['console_capture_errors']=console_errors
 text=(OUTPUT/'console.log').read_text(errors='replace') if (OUTPUT/'console.log').exists() else ''
 result['console_fault_lines']=[line for line in text.splitlines() if re.search(r'ASSERTION|FATAL|Fault|stack overflow|messages dropped',line,re.I)]
 result['passed']=(result.get('spike',{}).get('passed_runs')==49 and result.get('noise',{}).get('recorded_runs')==100 and 'final_status' in result and not any(result['final_status'].values()) and not result.get('failure') and not console_errors and not result['console_fault_lines'])
 result['finished_utc']=datetime.datetime.now(datetime.timezone.utc).isoformat()
 (OUTPUT/'results.json').write_text(json.dumps(result,indent=2)+'\n')
 print(json.dumps(result,indent=2),flush=True)
raise SystemExit(0 if result['passed'] else 1)
