import sys,time,json,threading
from pathlib import Path
sys.path.insert(0,'/Users/yuseungjae/Desktop/radar-track-fcc-hil')
import serial
from pc_sim.protocol import build_init_frame,build_radar_frame
from pc_sim.status_client import request_status
out=Path('/Users/yuseungjae/Desktop/radar-track-fcc-hil/docs/test-results/link-loss-2026-11-14')
phase=sys.argv[1]; raw=[]; traffic=[]; checks={}; stop=threading.Event()
class Wire(serial.Serial):
 def write(self,b):
  traffic.append({'direction':'TX','hex':b.hex(),'time':time.monotonic()});return super().write(b)
 def read(self,n=1):
  b=super().read(n)
  if b:traffic.append({'direction':'RX','hex':b.hex(),'time':time.monotonic()})
  return b
with serial.Serial('/dev/cu.usbmodem1203',115200,timeout=.05) as console,Wire('/dev/cu.usbserial-A5069RR4',115200,timeout=.05,write_timeout=.5) as data:
 def capture():
  while not stop.is_set():
   b=console.read(4096)
   if b:raw.append(b)
 def text():return b''.join(raw).decode(errors='replace').replace('main alive\r\n','').replace('main alive\n','')
 console.reset_input_buffer();thread=threading.Thread(target=capture);thread.start()
 try:
  if phase in ('rx_disconnected','tx_disconnected'):
   start=time.monotonic();timed_out=False
   try:request_status(data,timeout=1)
   except TimeoutError:timed_out=True
   elapsed=time.monotonic()-start;time.sleep(.2)
   checks={'pc_status_timeout':timed_out,'bounded_wait':.95<=elapsed<2,'request_reached_handler':('TYPE_STATUS' in text())==(phase=='tx_disconnected')}
  elif phase in ('rx_restored','tx_restored'):
   status=request_status(data);data.write(build_init_frame());time.sleep(.15)
   for i in range(5):data.write(build_radar_frame(i*100,1000,0));time.sleep(.1)
   time.sleep(.2)
   checks={'status_response':isinstance(status,dict),'tracking_init':text().count('Filters initialized at range:')==1,'four_updates':text().count('Measurement accepted')==4}
  elif phase=='tracking_cut':
   data.write(build_init_frame());time.sleep(.2)
   print('READY: streaming measurements; wait for CUT_NOW',flush=True)
   for i in range(30):data.write(build_radar_frame(i*100,1000,0));time.sleep(.1)
   print('CUT_NOW: disconnect adapter TX -> board RX; streaming continues until timeout or 120 seconds',flush=True)
   for i in range(30,1230):
    data.write(build_radar_frame(i*100,1000,0));time.sleep(.1)
    if 'Radar measurement timeout: track cleared' in text():
     time.sleep(1.3);break
   time.sleep(.3)
   checks={'tracking_started':'Measurement accepted' in text(),'timeout_once':text().count('Radar measurement timeout: track cleared')==1}
 finally:
  stop.set();thread.join(1)
  (out/(phase+'-console.log')).write_bytes(b''.join(raw))
  (out/(phase+'-traffic.json')).write_text(json.dumps(traffic,indent=2))
  (out/(phase+'-results.json')).write_text(json.dumps({'phase':phase,'checks':checks,'elapsed_s':locals().get('elapsed'),'status':locals().get('status')},indent=2))
print(json.dumps(checks),flush=True)
assert checks and all(checks.values())
