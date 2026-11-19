"""Capture the current GUI in preview mode; no serial ports or fake reports."""
import os
from pathlib import Path
import sys
import time

os.environ.setdefault('QT_QPA_PLATFORM', 'offscreen')
os.environ.setdefault('QT_SCALE_FACTOR', '2')
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

from PySide6.QtWidgets import QApplication
from pc_sim.gui import MainWindow


def wait_for(app, predicate, timeout=20):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        app.processEvents()
        if predicate():
            return
        time.sleep(0.01)
    raise RuntimeError('GUI capture timed out')


def main():
    app = QApplication.instance() or QApplication([])
    window = MainWindow()
    window.resize(1440, 900)
    window.show()
    try:
        # Use a repository-relative output setting in public screenshots.
        window.record_directory = Path('runs')
        window.record_label.setText('runs/')
        window.inputs['duration_s'].setValue(12)
        window.inputs['range_rate_mps'].setValue(-10)
        window.control_tabs.setCurrentIndex(1)
        window.log_tabs.setCurrentIndex(1)
        window.content_split.setSizes([680, 390])
        window.connect_button.click()
        wait_for(app, lambda: window.connected)
        window.start_button.click()
        wait_for(app, lambda: window.sample_count >= 110)
        window.stop_button.click()
        wait_for(app, lambda: window.start_button.isEnabled())
        window.draw_samples()
        app.processEvents()
        # A preview must never be presented as a board result.
        assert window.track_monitor.latest is None
        assert window.estimated_point.getData()[0] is None
        output = ROOT / 'docs/images'
        assert window.grab().save(str(output / 'radar-gui-preview.png'))
        window.control_tabs.setCurrentIndex(3)
        app.processEvents()
        assert window.grab().save(str(output / 'radar-gui-track-preview.png'))
        print(f'Captured current preview GUI: {window.sample_count} samples; no board reports')
    finally:
        window.close()
        wait_for(app, lambda: window.worker is None)


if __name__ == '__main__':
    main()
