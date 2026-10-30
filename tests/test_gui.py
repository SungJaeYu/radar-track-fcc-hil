"""Optional desktop tests: install pc_sim/requirements-gui.txt first."""
import os
import time
os.environ.setdefault('QT_QPA_PLATFORM', 'offscreen')

import pytest
pytest.importorskip('PySide6')
pytest.importorskip('pyqtgraph')
from PySide6.QtWidgets import QApplication
from pc_sim.gui import MainWindow
from pc_sim.gui_model import Settings
from pc_sim.gui_worker import SessionWorker
from pc_sim.protocol import build_init_frame


@pytest.fixture(scope='module')
def app():
    return QApplication.instance() or QApplication([])


def wait_for(app, predicate, timeout=3):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        app.processEvents()
        if predicate():
            return
        time.sleep(0.005)
    raise AssertionError('Timed out waiting for GUI state')


class FakeSerial:
    def __init__(self, *args, **kwargs):
        self.writes = []
        self.closed = False
        self.in_waiting = 0

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.closed = True

    def write(self, frame):
        self.writes.append(frame)
        return len(frame)

    def read(self, size):
        return b''


def test_gui_preview_start_stop_restart_and_close(app):
    window = MainWindow()
    window.show()
    try:
        assert not window.start_button.isEnabled()
        window.connect_button.click()
        wait_for(app, lambda: window.connected)
        window.start_button.click()
        wait_for(app, lambda: window.sample_count >= 2)
        window.stop_button.click()
        wait_for(app, lambda: window.start_button.isEnabled())
        count = window.sample_count
        wait_for(app, lambda: not window.stop_button.isEnabled())
        assert window.sample_count == count
        window.start_button.click()
        wait_for(app, lambda: window.sample_count >= 1 and window.stop_button.isEnabled())
        window.draw_samples()
        assert len(window.truth_curve.getData()[0]) > 0
        window.close()
        wait_for(app, lambda: window.worker is None)
    finally:
        if window.worker:
            window.worker.requestInterruption()
            window.worker.wait(2000)
            app.processEvents()
        window.close()


def test_gui_worker_init_restart_and_serial_cleanup(app):
    port = FakeSerial()
    worker = SessionWorker('fake', serial_factory=lambda *a, **kw: port)
    received, states = [], []
    worker.sample.connect(received.append)
    worker.running_changed.connect(states.append)
    worker.start()
    try:
        worker.start_simulation(Settings(noise_m=0, hz=20))
        wait_for(app, lambda: len(received) >= 2)
        worker.stop_simulation()
        wait_for(app, lambda: states and states[-1] is False)
        before = len(port.writes)
        worker.start_simulation(Settings(noise_m=0))
        wait_for(app, lambda: len(port.writes) >= before + 2)
        assert port.writes[0] == build_init_frame()
        assert port.writes[before] == build_init_frame()
        assert sum(f == build_init_frame() for f in port.writes) == 2
    finally:
        worker.requestInterruption()
        assert worker.wait(2000)
    assert port.closed


def test_gui_worker_write_failure_closes_port(app):
    class BrokenSerial(FakeSerial):
        def write(self, frame):
            raise OSError('disconnected')
    port = BrokenSerial()
    errors = []
    worker = SessionWorker('fake', serial_factory=lambda *a, **kw: port)
    worker.error.connect(errors.append)
    worker.start_simulation(Settings())
    worker.start()
    try:
        wait_for(app, lambda: bool(errors) and not worker.isRunning())
        assert 'disconnected' in errors[0]
        assert port.closed
    finally:
        worker.requestInterruption()
        worker.wait(2000)


def test_gui_same_port_rejected_before_open(app):
    calls = []
    worker = SessionWorker('same', 'same', serial_factory=lambda *a, **kw: calls.append(a))
    errors = []
    worker.error.connect(errors.append)
    worker.start()
    try:
        wait_for(app, lambda: bool(errors) and not worker.isRunning())
        assert calls == []
    finally:
        worker.requestInterruption()
        worker.wait(2000)


def test_gui_settings_remain_accessible_in_short_window(app):
    window = MainWindow()
    window.resize(1000, 650)
    window.show()
    try:
        app.processEvents()
        assert window.height() <= 650
        from PySide6.QtCore import Qt
        assert window.content_split.orientation() == Qt.Orientation.Horizontal
        assert window.plots_split.orientation() == Qt.Orientation.Vertical
        assert window.log_tabs.height() >= window.plots_split.height() - 2
        window.control_tabs.setCurrentIndex(1)
        app.processEvents()
        fields = list(window.inputs.values())
        for field in fields:
            assert field.height() >= field.minimumSizeHint().height()
        for previous, following in zip(fields, fields[1:]):
            assert previous.geometry().bottom() < following.geometry().top()
        window.control_tabs.setCurrentIndex(1)
        app.processEvents()
        window.controls_scroll.ensureWidgetVisible(fields[-1])
        app.processEvents()
        from PySide6.QtCore import QPoint
        viewport = window.controls_scroll.viewport()
        position = fields[-1].mapTo(viewport, QPoint(0, 0))
        assert position.y() >= 0
        assert position.y() + fields[-1].height() <= viewport.height()
        assert window.start_button.isVisible()
        assert window.stop_button.isVisible()
        assert window.stop_button.mapTo(window, QPoint(0, 0)).y() + window.stop_button.height() <= window.height()
    finally:
        window.close()
