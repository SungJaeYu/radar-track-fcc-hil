import sys, time, json, math, hashlib
from pathlib import Path
sys.path.insert(0, '/Users/yuseungjae/Desktop/radar-track-fcc-hil')
import serial
from pc_sim.protocol import build_init_frame, build_radar_frame_from_radians
from pc_sim.status_client import request_status
root=Path('/Users/yuseungjae/Desktop/radar-track-fcc-hil')
mode=sys.argv[1]
gate_m,gate_deg=(100,10) if mode=='normal' else (250,30)
build=root/'build' if mode=='normal' else Path('/private/tmp/radar-gate-config')
out=root/'docs/test-results/gating-2026-11-14'/mode
out.mkdir(parents=True,exist_ok=True)
raw=bytearray(); cases=[]; traffic=[]
with serial.Serial('/dev/cu.usbmodem1203',115200,timeout=0.02) as console, serial.Serial('/dev/cu.usbserial-A5069RR4',115200,timeout=0.05) as data:
    def drain(duration):
        end=time.monotonic()+duration
        while time.monotonic()<end: raw.extend(console.read(4096))
    def send(frame):
        traffic.append(frame.hex());data.write(frame);data.flush()
    def run(name,delta_m,initial_deg,measured_deg,expected):
        begin=len(raw)
        send(build_init_frame());drain(.08)
        send(build_radar_frame_from_radians(0,1000,math.radians(initial_deg)));drain(.08)
        send(build_radar_frame_from_radians(100,1000+delta_m,math.radians(measured_deg)));drain(.15)
        text=raw[begin:].decode(errors='replace').replace('main alive\r\n','').replace('main alive\n','')
        accepted=text.count('Measurement accepted');rejected=text.count('Measurement rejected by gate')
        passed=(accepted==int(expected) and rejected==int(not expected) and text.count('Filters initialized at range:')==1 and 'ASSERTION FAIL' not in text)
        cases.append(dict(name=name,range_error_m=delta_m,initial_deg=initial_deg,measured_deg=measured_deg,expected_accept=expected,accepted=accepted,rejected=rejected,passed=passed))
    console.reset_input_buffer();before=request_status(data);drain(.1)
    try:
        for sign in (-1,1):
            run(f'range_boundary_{sign}',sign*gate_m,0,0,True)
            run(f'range_outside_{sign}',sign*(gate_m+1),0,0,False)
            # The wire quantizes angle; use a margin rather than claiming an exact float boundary.
            run(f'angle_inside_{sign}',0,0,sign*(gate_deg-.1),True)
            run(f'angle_outside_{sign}',0,0,sign*(gate_deg+.1),False)
        run('both_inside',gate_m-1,0,gate_deg-.1,True)
        run('both_outside',gate_m+1,0,gate_deg+.1,False)
        run('wrap_positive',0,179,-179,True)
        run('wrap_negative',0,-179,179,True)
        run('same_input_range_150',150,0,0,gate_m>=150)
        run('same_input_angle_20',0,0,20,gate_deg>=20)
        after=request_status(data);drain(.1)
        checks={'all_cases_passed':all(c['passed'] for c in cases),'stats_unchanged':before==after,'no_assert':'ASSERTION FAIL' not in raw.decode(errors='replace')}
    finally:
        (out/'gate-console.log').write_bytes(raw)
        (out/'gate-traffic.json').write_text(json.dumps(traffic,indent=2))
        (out/'gate-results.json').write_text(json.dumps(dict(revision='610e01e',mode=mode,gate_m=gate_m,gate_deg=gate_deg,cases=cases,checks=locals().get('checks',{}),before=before,after=locals().get('after'),hex_sha256=hashlib.sha256((build/'zephyr/zephyr.hex').read_bytes()).hexdigest()),indent=2))
print(json.dumps(checks));assert all(checks.values()),cases
