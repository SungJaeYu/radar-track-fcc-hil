"""Focused real-board regression for e66515e; run from the repository root.

PYTHONPATH=. python docs/test-results/track-2026-11-18/uart-check.py
Uses the named data/console ports exclusively and sends INIT/RESET commands.
"""
from pathlib import Path
from dataclasses import asdict
import json
import time
import serial
from pc_sim.frame_parser import FrameParser
from pc_sim.protocol import encode_frame, build_measurement_frame, decode_track_payload, decode_status_payload

folder = Path(__file__).resolve().parent
parser = FrameParser()
checks, records, reports = [], [], []
console_bytes = bytearray()
started = time.monotonic()
def check(name, condition):
    checks.append({'name': name, 'passed': bool(condition)})
    assert condition, name

def event_reset(report):
    return report.state == 0 and report.result == 4 and not report.input_present and not report.estimate_valid and report.input_timestamp_ms == report.estimate_timestamp_ms == 0

try:
    with serial.Serial('/dev/cu.usbserial-A5069RR4',115200,timeout=0.01,write_timeout=1) as data, serial.Serial('/dev/cu.usbmodem1203',115200,timeout=0) as console:
        def collect(duration):
            frames=[]
            end=time.monotonic()+duration
            while time.monotonic()<end:
                raw=data.read(data.in_waiting or 1)
                console_bytes.extend(console.read(console.in_waiting))
                if raw:
                    records.append({'t':time.monotonic()-started,'direction':'rx','hex':raw.hex()})
                    frames.extend(parser.feed(raw))
            for frame in frames:
                if frame.frame_type==7:
                    report=decode_track_payload(frame.payload)
                    reports.append(asdict(report))
            return frames
        def send(raw):
            records.append({'t':time.monotonic()-started,'direction':'tx','hex':raw.hex()})
            assert data.write(raw)==len(raw)
            data.flush()
        def request(raw):
            send(raw)
            return collect(0.12)
        def track(raw):
            frames=request(raw)
            check('single TRACK reply',len(frames)==1 and frames[0].frame_type==7)
            return decode_track_payload(frames[0].payload)
        def measurement(t,r=1000):
            return track(build_measurement_frame(t,r,0))
        def status():
            frames=request(encode_frame(3,b'\0'))
            check('single STATUS reply',len(frames)==1 and frames[0].frame_type==6)
            return decode_status_payload(frames[0].payload)
        collect(0.1)
        check('INIT event',event_reset(track(encode_frame(1,b'\0'))))
        check('no timeout before first measurement',collect(1.2)==[])
        baseline=status()
        r=measurement(0)
        check('STARTED at timestamp zero',r.result==0 and r.state==1 and r.input_present and r.estimate_valid and r.estimate_timestamp_ms==0)
        check('constant range decoded',abs(r.range_m-1000)<0.01 and abs(r.azimuth_rad)<0.0001)
        measurement(100)
        check('CONFIRMED after three hits',measurement(200).state==2)
        r=measurement(200)
        check('duplicate timestamp rejected preserving estimate',r.result==3 and r.estimate_timestamp_ms==200 and r.estimate_valid)
        r=measurement(150)
        check('backward timestamp rejected preserving estimate',r.result==3 and r.input_timestamp_ms==150 and r.estimate_timestamp_ms==200)
        r=measurement(300,2000)
        check('gate rejection coasts',r.result==2 and r.state==3 and r.estimate_timestamp_ms==300)
        check('gate recovery',measurement(400).state==2)
        check('invalid semantic payload has no report',request(build_measurement_frame(500,0,0))==[])
        check('invalid payload size has no report',request(encode_frame(2,bytes(9)))==[])
        frames=collect(1.2)
        check('one timeout event',len(frames)==1 and frames[0].frame_type==7 and event_reset(decode_track_payload(frames[0].payload)))
        check('no repeat timeout event',collect(1.2)==[])
        check('measurement restarts after timeout',measurement(0).result==0)
        check('RESET event',event_reset(track(encode_frame(5,b'\0'))))
        check('RESET disarms timeout',collect(1.2)==[])
        measurement(100)
        r=measurement(1101)
        check('long sensor gap RESET keeps input flag',r.result==4 and r.input_present and not r.estimate_valid and r.state==0)
        check('next measurement starts after gap',measurement(1201).result==0)
        check('INIT clears active track',event_reset(track(encode_frame(1,b'\0'))))
        measurement(0)
        measurement(100)
        measurement(200)
        for i in range(5):
            r=measurement(300+i*100,2000)
        check('five gate misses LOST',r.state==4 and not r.estimate_valid)
        r=measurement(800)
        check('LOST prediction resets',r.result==4 and r.state==0 and r.input_present)
        check('restart after LOST reset',measurement(900).result==0)
        track(encode_frame(5,b'\0'))
        corrupt=bytearray(build_measurement_frame(1000,1000,0));corrupt[-1]^=1
        check('bad CRC no TRACK',request(bytes(corrupt))==[])
        check('bad LEN no TRACK',request(b'\x7e\x00')==[])
        send(build_measurement_frame(1100,1000,0)[:7])
        check('partial frame timeout no TRACK',collect(1.2)==[])
        stats=status()
        check('CRC counter +1',stats['crc_errors']==baseline['crc_errors']+1)
        check('length counter +1',stats['length_errors']==baseline['length_errors']+1)
        check('partial frame timeout counter +1',stats['frame_timeouts']==baseline['frame_timeouts']+1)
        check('no queue or overflow drops',all(stats[k]==baseline[k] for k in ['queue_drops','ring_overflow_events','rx_dropped_bytes','recovery_discarded_bytes']))
        check('receive recovery after malformed frames',measurement(0).result==0)
        # Interleave replies in one read stream and validate both types.
        send(build_measurement_frame(100,1000,0)+encode_frame(3,b'\0'))
        frames=collect(0.2)
        check('mixed TRACK and STATUS', [f.frame_type for f in frames]==[7,6])
        check('mixed report values',decode_track_payload(frames[0].payload).result==1 and decode_status_payload(frames[1].payload)==stats)
        track(encode_frame(5,b'\0'))
        seq=[r['report_sequence'] for r in reports]
        check('all observed TRACK sequences consecutive',all(b==(a+1)&0xffffffff for a,b in zip(seq,seq[1:])))
        check('PC receive framing errors zero',parser.crc_errors==parser.length_errors==0)
        check('no repeat final RESET',collect(1.2)==[])
finally:
    (folder/'wire.json').write_text(json.dumps(records,indent=2)+'\n')
    (folder/'console.log').write_bytes(console_bytes)
    (folder/'results.json').write_text(json.dumps({'revision':'e66515e','checks':checks,'reports':reports,'duration_s':time.monotonic()-started},indent=2)+'\n')
print(f'PASS: {len(checks)} checks; {len(reports)} TRACK reports')
