import sys,time,json
from pathlib import Path
sys.path.insert(0,'/Users/yuseungjae/Desktop/radar-track-fcc-hil')
from PySide6.QtWidgets import QApplication
from pc_sim.gui import MainWindow
app=QApplication([]);w=MainWindow();w.show()
out=Path('/Users/yuseungjae/Desktop/radar-track-fcc-hil/docs/test-results/status-extended-2026-11-14/normal')
def wait(predicate,seconds=4):
    end=time.monotonic()+seconds
    while time.monotonic()<end:
        app.processEvents()
        if predicate():return
        time.sleep(.01)
    raise AssertionError('GUI condition timeout')
result={}
try:
    w.mode.setCurrentIndex(1)
    for combo,name in [(w.data_port,'/dev/cu.usbserial-A5069RR4'),(w.console_port,'/dev/cu.usbmodem1203')]:
        index=combo.findData(name);assert index>=0;combo.setCurrentIndex(index)
    w.connect_button.click();wait(lambda:w.connected)
    w.control_tabs.setCurrentIndex(2)
    w.status_button.click();wait(lambda:w.status_message.text()=='조회 성공')
    stats={k:int(v.text()) for k,v in w.status_values.items()}
    assert stats=={'crc_errors':1,'length_errors':2,'frame_timeouts':1,'queue_drops':0,'ring_overflow_events':0,'rx_dropped_bytes':0,'recovery_discarded_bytes':0}
    result['actual_board_stats']=stats
    w.start_button.click();wait(lambda:w.sample_count>=3)
    w.status_button.click();wait(lambda:w.status_message.text()=='조회 성공')
    count=w.sample_count;wait(lambda:w.sample_count>count)
    result['status_during_measurements']=True
    w.grab().save(str(out/'gui-status.png'))
    w.stop_button.click();wait(lambda:w.start_button.isEnabled())
    w.status_button.click();wait(lambda:w.status_message.text()=='조회 성공')
    result['status_after_stop']=True
    (out/'gui-console.log').write_text(w.console_text.toPlainText())
finally:
    w.close();wait(lambda:w.worker is None)
    result['connection_closed']=True
    (out/'gui-results.json').write_text(json.dumps(result,indent=2))
print(json.dumps(result,indent=2))
