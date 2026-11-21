import os
os.environ['QT_QPA_PLATFORM'] = 'offscreen'
import json
from pathlib import Path
import sys
import time
sys.path.insert(0, '/Users/yuseungjae/Desktop/radar-track-fcc-hil')
from PySide6.QtWidgets import QApplication
from pc_sim.gui import MainWindow

out = Path('/private/tmp/radar-log-format-gui-20261122')
out.mkdir(exist_ok=False)
app = QApplication([])
window = MainWindow()
errors, statuses, tracks = [], [], []

def wait(predicate, timeout=6):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        app.processEvents()
        if predicate():
            return
        time.sleep(.005)
    raise AssertionError('GUI condition timeout')

try:
    window.mode.setCurrentIndex(1)
    for combo, port in [(window.data_port, '/dev/cu.usbserial-A5069RR4'),
                        (window.console_port, '/dev/cu.usbmodem1203')]:
        index = combo.findData(port)
        assert index >= 0
        combo.setCurrentIndex(index)
    window.inputs['range_rate_mps'].setValue(0)
    window.inputs['range_noise_std_m'].setValue(0)
    window.inputs['duration_s'].setValue(3)
    window.inputs['sample_rate_hz'].setValue(10)
    window.record_directory = out / 'session'
    window.record_checkbox.setChecked(True)
    window.show()
    window.connect_button.click()
    wait(lambda: window.connected)
    window.worker.error.connect(errors.append)
    window.worker.status_received.connect(statuses.append)
    window.worker.track_received.connect(lambda report, at: tracks.append(report))
    window.start_button.click()
    wait(lambda: window.sample_count >= 10 and window.track_monitor.latest is not None)
    window.status_button.click()
    wait(lambda: len(statuses) == 1)
    wait(lambda: window.start_button.isEnabled())
    window.draw_samples()
    console = window.console_text.toPlainText()
    assert 'INIT: track=reset measurement_monitor=reset' in console
    assert 'Track started: state=TENTATIVE sensor_ms=' in console
    assert 'Track state: previous=TENTATIVE current=CONFIRMED' in console
    assert not errors and not any(statuses[0].values())
    assert window.track_monitor.latest.estimate_valid
    assert abs(window.track_monitor.latest.range_m - 1000) < .01
    assert len(tracks) >= 20
    window.grab().save(str(out / 'gui.png'))
    (out / 'console.log').write_text(console)
    result = {'passed': True, 'qt_platform': 'offscreen', 'hardware': 'STM32F746G-DISCO',
              'samples': window.sample_count, 'track_reports': len(tracks), 'status': statuses[0],
              'console_display_checked': True, 'estimate_display_checked': True, 'errors': errors}
    (out / 'result.json').write_text(json.dumps(result, indent=2))
    print(json.dumps(result, indent=2))
finally:
    if window.worker:
        window.worker.requestInterruption()
        window.worker.wait(3000)
    app.processEvents()
    window.close()
