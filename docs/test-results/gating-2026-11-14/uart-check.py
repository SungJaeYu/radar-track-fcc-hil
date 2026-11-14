import sys
sys.path.insert(0, '/Users/yuseungjae/Desktop/radar-track-fcc-hil')
import json, time, threading, hashlib
from datetime import datetime
from pathlib import Path
import serial
from pc_sim.status_client import request_status
from pc_sim.protocol import encode_frame, build_radar_frame
root=Path('/Users/yuseungjae/Desktop/radar-track-fcc-hil')
policy=sys.argv[1]
build_path=root/'build' if policy == 'normal' else Path('/private/tmp/radar-gate-config')
out=root/'docs/test-results/gating-2026-11-14'/policy
out.mkdir(parents=True, exist_ok=True)
traffic=[]; logs=[]; stop=threading.Event(); results={}
class LoggedSerial(serial.Serial):
    def write(self,data):
        traffic.append({'time':time.monotonic(),'direction':'TX','hex':bytes(data).hex(' ')})
        return super().write(data)
    def read(self,size=1):
        data=super().read(size)
        if data: traffic.append({'time':time.monotonic(),'direction':'RX','hex':data.hex(' ')})
        return data

def capture(port):
    while not stop.is_set():
        raw=port.read(4096)
        if raw: logs.append(raw.decode('utf-8',errors='replace'))

def raw_log(): return ''.join(logs)
def log(): return raw_log().replace('main alive\r\n', '').replace('main alive\n', '')
def check(name,condition):
    results[name]=bool(condition)
    if not condition: raise AssertionError(name)

try:
    with serial.Serial('/dev/cu.usbmodem1203',115200,timeout=0.05) as console, LoggedSerial('/dev/cu.usbserial-A5069RR4',115200,timeout=0.05,write_timeout=0.5) as data:
        thread=threading.Thread(target=capture,args=(console,),daemon=True);thread.start()
        try:
            data.write(encode_frame(1,b'\0'));time.sleep(1.2)
            check('no_timeout_before_first_measurement','Radar measurement timeout' not in log())
            initial=request_status(data)
            repeat=[request_status(data) for _ in range(3)]
            check('repeated_status_unchanged',all(x==initial for x in repeat))
            corrupt=bytearray(build_radar_frame(100,1000,0));corrupt[-1]^=1
            data.write(corrupt);time.sleep(0.1)
            crc=request_status(data)
            check('crc_error_increment',crc==dict(initial,crc_errors=initial['crc_errors']+1))
            data.write(b'\x7e\x00\x7e\x21');time.sleep(0.1)
            length=request_status(data)
            check('length_error_increment',length==dict(crc,length_errors=crc['length_errors']+2))
            data.write(build_radar_frame(100,1000,0)[:7]);time.sleep(1.3)
            partial=request_status(data)
            check('partial_timeout_increment',partial==dict(length,frame_timeouts=length['frame_timeouts']+1))
            data.write(encode_frame(5,b'\0'));time.sleep(0.1)
            check('reset_preserves_counters',request_status(data)==partial)
            data.write(encode_frame(1,b'\0'));time.sleep(0.1)
            check('init_preserves_counters',request_status(data)==partial)
            begin=len(log())
            for i in range(10):
                data.write(build_radar_frame(i*100,1000,0))
                if i in (2,5,8): check('status_during_tracking_'+str(i),request_status(data)==partial)
                time.sleep(0.1)
            time.sleep(0.1)
            tracking=log()[begin:]
            check('tracking_initializes_once',tracking.count('Filters initialized at range:')==1)
            check('tracking_accepts_nine_updates',tracking.count('Measurement accepted')==9)
            check('no_timeout_while_measuring','Radar measurement timeout' not in tracking)
            # STATUS traffic alone must not keep the track alive.
            for _ in range(7):
                request_status(data);time.sleep(0.2)
            check('status_does_not_refresh_measurement_timeout',log()[begin:].count('Radar measurement timeout: track cleared')==1)
            time.sleep(0.3)
            check('measurement_timeout_is_one_shot',log()[begin:].count('Radar measurement timeout: track cleared')==1)
            check('final_status_available',request_status(data)==partial)
            results['initial_stats']=initial;results['final_stats']=partial
        finally:
            stop.set();thread.join(1)
finally:
    results['base_revision']='610e01e'
    results['source_state']='committed gate configuration; see firmware_source_sha256'
    results['firmware_source_sha256']={str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted((root/'fcc_app/src').rglob('*')) if p.is_file()}
    results['config_sha256']=hashlib.sha256((build_path/'zephyr/.config').read_bytes()).hexdigest()
    results['policy']=policy
    results['recorded_at']=datetime.now().astimezone().isoformat()
    results['firmware_hex_sha256']=hashlib.sha256((build_path/'zephyr/zephyr.hex').read_bytes()).hexdigest()
    (out/'console.log').write_text(raw_log())
    (out/'traffic.json').write_text(json.dumps(traffic,indent=2))
    (out/'results.json').write_text(json.dumps(results,indent=2))
    print(json.dumps(results,indent=2))
