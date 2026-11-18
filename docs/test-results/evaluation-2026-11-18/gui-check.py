"""Offscreen Qt widgets and real UART worker; no simulated board responses."""
import os
os.environ['QT_QPA_PLATFORM'] = 'offscreen'
from pathlib import Path
import json
import time
from PySide6.QtWidgets import QApplication
from pc_sim.gui import MainWindow
from pc_sim.recording import evaluate_session

root = Path(__file__).resolve().parent
app = QApplication([])
window = MainWindow()
checks=[]
def check(name, condition):
    checks.append({'name':name,'passed':bool(condition)})
    assert condition, name

def wait(predicate, timeout=6):
    end=time.monotonic()+timeout
    while time.monotonic()<end:
        app.processEvents()
        if predicate(): return
        time.sleep(.01)
    raise AssertionError('GUI wait timed out')

try:
    window.show()
    window.resize(1280,850)
    window.mode.setCurrentIndex(1)
    for combo, device in [(window.data_port,'/dev/cu.usbserial-A5069RR4'),
                          (window.console_port,'/dev/cu.usbmodem11203')]:
        combo.addItem(device,device)
        combo.setCurrentIndex(combo.count()-1)
    window.record_directory=root/'gui'
    window.record_checkbox.setChecked(True)
    window.inputs['range_noise_std_m'].setValue(8)
    window.connect_button.click()
    wait(lambda:window.connected)
    window.start_button.click()
    wait(lambda:window.sample_count>=35 and window.track_monitor.latest is not None)
    window.draw_samples()
    check('board CONFIRMED',window.track_values['state'].text()=='CONFIRMED')
    check('board curve exists',len(window.estimated_curve.getData()[0])>=30)
    check('live board position exists',len(window.estimated_point.getData()[0])==1)
    first_path=Path(window.record_label.text())
    window.request_status()
    wait(lambda:window.status_message.text()=='조회 성공')
    check('32-byte STATUS stale counter displayed',window.status_values['stale_measurements'].text()=='0')
    window.control_tabs.setCurrentIndex(3)
    app.processEvents()
    window.grab().save(str(root/'gui-track.png'))
    window.resize(1000,650)
    app.processEvents()
    check('small window remains 1000x650',window.width()==1000 and window.height()==650)
    check('track panel fits width',window.track_scroll.widget().width()<=window.track_scroll.viewport().width())
    window.grab().save(str(root/'gui-track-small.png'))
    window.stop_button.click()
    wait(lambda:window.start_button.isEnabled())
    wait(lambda:window.track_monitor.latest is not None and not window.track_monitor.latest.estimate_valid)
    window.draw_samples()
    check('timeout clears live estimate',window.estimated_point.getData()[0] is None)
    check('timeout reports EMPTY',window.track_values['state'].text()=='EMPTY')
    wait(lambda:'STALE' in window.track_message.text())
    check('STALE keeps last board state',window.track_values['state'].text()=='EMPTY')
    window.grab().save(str(root/'gui-stale.png'))
    window.start_button.click()
    wait(lambda:window.sample_count>=3 and window.track_monitor.latest is not None and window.track_monitor.latest.estimate_valid)
    check('restart makes separate CSV directory',Path(window.record_label.text())!=first_path)
    check('previous recording closed',json.loads((first_path/'metadata.json').read_text())['closed'])
    second_path=Path(window.record_label.text())
    window.close()
    wait(lambda:window.worker is None)
    check('disconnect closes recording',json.loads((second_path/'metadata.json').read_text())['closed'])
    evaluation=evaluate_session(first_path)
    check('recording has matched post-warmup samples',evaluation['matched_estimates']>0)
    (root/'gui-evaluation.json').write_text(json.dumps(evaluation,indent=2)+'\n')
finally:
    if window.worker:
        window.worker.requestInterruption()
        window.worker.wait(3000)
        app.processEvents()
    window.close()
    (root/'gui-results.json').write_text(json.dumps(checks,indent=2)+'\n')
print('PASS',len(checks),'real UART GUI checks')
