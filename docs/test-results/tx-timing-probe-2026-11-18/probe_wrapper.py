"""Profile the unchanged bench workflow with a temporary cycle-timed TX image."""
from pathlib import Path
from collections import defaultdict
import argparse
import difflib
import gc
import importlib.util
import json
import statistics
import time

ROOT = Path.cwd()
SOURCE = ROOT/'fcc_app/tests/test_perf_monitor/board_check.py'
probe_path=Path('/private/tmp/radar_pc_probe_workflow.py')
probe_path.write_text(SOURCE.read_text().replace('ROOT = Path(__file__).resolve().parents[3]','ROOT = Path.cwd()').replace('build_perf','build_tx_probe'))
spec=importlib.util.spec_from_file_location('probe_workflow',probe_path)
bench=importlib.util.module_from_spec(spec);spec.loader.exec_module(bench)
BASE_SERIAL=bench.serial.Serial
BASE_RECORDER=bench.SessionRecorder
real_sleep=time.sleep
stats=defaultdict(lambda:{'count':0,'total_ms':0.,'max_ms':0.,'max_cpu_ms':0.,'max_overrun_ms':0.})
events=[]

def measured(name, call, requested_s=0):
    started=time.monotonic();cpu=time.thread_time_ns()
    try:
        return call()
    finally:
        elapsed=(time.monotonic()-started)*1000
        cpu_ms=(time.thread_time_ns()-cpu)/1e6
        item=stats[name];item['count']+=1;item['total_ms']+=elapsed
        item['max_ms']=max(item['max_ms'],elapsed);item['max_cpu_ms']=max(item['max_cpu_ms'],cpu_ms)
        overrun=elapsed-requested_s*1000
        item['max_overrun_ms']=max(item['max_overrun_ms'],overrun)
        if overrun>=5:
            events.append({'name':name,'at_monotonic_s':started,'wall_ms':elapsed,'cpu_ms':cpu_ms,'requested_ms':requested_s*1000,'overrun_ms':overrun})

class SerialProbe(BASE_SERIAL):
    def read(self,size=1):
        return measured('serial_read:'+str(self.port),lambda:super(SerialProbe,self).read(size))
    def write(self,data):
        return measured('serial_write:'+str(self.port),lambda:super(SerialProbe,self).write(data))
    @property
    def in_waiting(self):
        return measured('serial_in_waiting:'+str(self.port),lambda:super(SerialProbe,self).in_waiting)

class FlushProbe:
    def __init__(self,stream,name):self.stream,self.name=stream,name
    def flush(self):return measured('csv_flush:'+self.name,self.stream.flush)

class RecorderProbe(BASE_RECORDER):
    def __init__(self,*args,**kwargs):
        super().__init__(*args,**kwargs)
        self._files={name:FlushProbe(stream,name) for name,stream in self._files.items()}
    def _row(self,name,row):
        return measured('record_row:'+name,lambda:super(RecorderProbe,self)._row(name,row))

class TimeProbe:
    def __getattr__(self,name):return getattr(time,name)
    def sleep(self,seconds):return measured('sleep',lambda:real_sleep(seconds),requested_s=seconds)

gc_starts={}
def gc_probe(phase,info):
    generation=info['generation']
    if phase=='start':gc_starts[generation]=(time.monotonic(),time.thread_time_ns())
    elif generation in gc_starts:
        start,cpu=gc_starts.pop(generation);wall=(time.monotonic()-start)*1000;cpu_ms=(time.thread_time_ns()-cpu)/1e6
        item=stats['gc:'+str(generation)];item['count']+=1;item['total_ms']+=wall;item['max_ms']=max(item['max_ms'],wall);item['max_cpu_ms']=max(item['max_cpu_ms'],cpu_ms)
        if wall>=5:events.append({'name':'gc:'+str(generation),'at_monotonic_s':start,'wall_ms':wall,'cpu_ms':cpu_ms})

bench.serial.Serial=SerialProbe
bench.SessionRecorder=RecorderProbe
bench.time=TimeProbe()
bench.sum=lambda iterable:measured('sum_progress_or_check',lambda:sum(iterable))
bench.print=lambda *args,**kwargs:measured('print',lambda:print(*args,**kwargs))
gc.callbacks.append(gc_probe)
args=argparse.Namespace(data_port='/dev/cu.usbserial-A5069RR4',console_port='/dev/cu.usbmodem11203',
    output=ROOT/'docs/test-results/tx-timing-probe-2026-11-18',workspace=Path('/Users/yuseungjae/zephyrproject'),
    west='/Users/yuseungjae/zephyrproject/.venv/bin/west',git='/Applications/Xcode.app/Contents/Developer/usr/bin/git',duration=120,smoke=False)
try:
    result=bench.run(args)
finally:
    gc.callbacks.remove(gc_probe)
    args.output.mkdir(parents=True,exist_ok=True)
    (args.output/'pc-operations.json').write_text(json.dumps({'stats':stats,'slow_events':events},indent=2)+'\n')
    (args.output/'probe_wrapper.py').write_text(Path(__file__).read_text())
    (args.output/'probe_workflow.py').write_text(probe_path.read_text())
    patch=''
    for filename in ['src/main.c','src/transport/uart_tx.c']:
        before=(ROOT/'fcc_app'/filename).read_text().splitlines(keepends=True)
        after=(Path('/private/tmp/radar-tx-probe-app')/filename).read_text().splitlines(keepends=True)
        patch+=''.join(difflib.unified_diff(before,after,fromfile='fcc_app/'+filename,tofile='probe/'+filename))
    (args.output/'temporary-firmware.patch').write_text(patch)
    (args.output/'progress-sum-benchmark.json').write_text(Path('/private/tmp/radar_progress_bench.json').read_text())
print('PC operation profile saved',args.output/'pc-operations.json')
raise SystemExit(result)
