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


class StatusSerial(FakeSerial):
    def __init__(self, reply=True):
        super().__init__()
        self.reply = reply
        self.buffer = bytearray()

    def write(self, frame):
        from pc_sim.protocol import FRAME_TYPE_STATUS_REQUEST
        result = super().write(frame)
        if frame[2] == FRAME_TYPE_STATUS_REQUEST and self.reply:
            # Fixed wire reference; read() deliberately fragments this response.
            self.buffer.extend(bytes.fromhex(
                '7e 10 06 12 34 56 78 80 00 00 00 00 00 00 01 ff ff ff ff eb 11'))
            self.in_waiting = len(self.buffer)
        return result

    def read(self, size):
        count = min(size, 3)
        raw = bytes(self.buffer[:count])
        del self.buffer[:count]
        self.in_waiting = len(self.buffer)
        return raw


def test_gui_status_success_timeout_and_retry_during_measurements(app, monkeypatch):
    from pc_sim import gui
    port = StatusSerial()

    def make_worker(data, console, parent):
        worker = SessionWorker(data, console, parent, serial_factory=lambda *a, **kw: port)
        worker.STATUS_TIMEOUT_S = 0.15
        return worker

    monkeypatch.setattr(gui, 'SessionWorker', make_worker)
    window = MainWindow()
    window.show()
    try:
        assert not window.status_button.isEnabled()
        window.mode.setCurrentIndex(1)
        window.data_port.addItem('fake', 'fake')
        window.data_port.setCurrentIndex(window.data_port.count() - 1)
        window.connect_button.click()
        wait_for(app, lambda: window.connected)
        window.start_button.click()
        wait_for(app, lambda: window.sample_count >= 1)

        window.request_status()
        window.request_status()
        assert not window.status_button.isEnabled()
        wait_for(app, lambda: window.status_message.text() == '조회 성공')
        assert window.status_values['crc_errors'].text() == '305419896'
        assert window.status_values['queue_drops'].text() == '4294967295'
        assert window.status_updated.text() != '마지막 성공 조회: —'
        assert sum(frame[2] == 3 for frame in port.writes) == 1
        assert window.stop_button.isEnabled()

        port.reply = False
        count = window.sample_count
        window.request_status()
        wait_for(app, lambda: '시간 초과' in window.status_message.text())
        wait_for(app, lambda: window.sample_count > count)
        assert window.status_values['crc_errors'].text() == '305419896'
        assert window.status_button.isEnabled()
        port.reply = True
        window.request_status()
        wait_for(app, lambda: window.status_message.text() == '조회 성공')
        assert sum(frame[2] == 3 for frame in port.writes) == 3
    finally:
        window.close()
        wait_for(app, lambda: window.worker is None)
    assert port.closed


def test_gui_disconnect_cancels_status(app, monkeypatch):
    from pc_sim import gui
    port = StatusSerial(reply=False)
    monkeypatch.setattr(gui, 'SessionWorker', lambda data, console, parent:
        SessionWorker(data, console, parent, serial_factory=lambda *a, **kw: port))
    window = MainWindow()
    try:
        window.mode.setCurrentIndex(1)
        window.data_port.addItem('fake', 'fake')
        window.data_port.setCurrentIndex(window.data_port.count() - 1)
        window.connect_button.click()
        wait_for(app, lambda: window.connected)
        window.request_status()
        wait_for(app, lambda: bool(port.writes))
        window.disconnect()
        wait_for(app, lambda: window.worker is None)
        assert not window.status_pending
        assert not window.status_button.isEnabled()
        assert port.closed
    finally:
        window.close()
        wait_for(app, lambda: window.worker is None)


def test_worker_status_skips_invalid_frames_and_rejects_duplicates(app):
    from pc_sim.protocol import encode_frame
    port = StatusSerial(reply=False)
    worker = SessionWorker('fake', serial_factory=lambda *a, **kw: port)
    reports = []
    worker.status_received.connect(reports.append)
    assert worker.request_status()
    assert not worker.request_status()
    worker.start()
    try:
        wait_for(app, lambda: bool(port.writes))
        valid = bytes.fromhex(
            '7e 10 06 12 34 56 78 80 00 00 00 00 00 00 01 ff ff ff ff eb 11')
        corrupt = bytearray(valid)
        corrupt[-1] ^= 1
        port.buffer.extend(b'\x7e\x00' + bytes(corrupt) + encode_frame(1, b'\x00')
                           + encode_frame(6, b'\x00') + valid)
        port.in_waiting = len(port.buffer)
        wait_for(app, lambda: bool(reports))
        assert reports == [{'crc_errors': 0x12345678, 'length_errors': 0x80000000,
                            'frame_timeouts': 1, 'queue_drops': 0xffffffff}]
        assert not worker.status_pending.is_set()
        assert len(port.writes) == 1
    finally:
        worker.requestInterruption()
        assert worker.wait(2000)
    assert port.closed


def test_preview_rejects_status_request(app):
    worker = SessionWorker()
    assert not worker.request_status()
    assert worker.commands.empty()
