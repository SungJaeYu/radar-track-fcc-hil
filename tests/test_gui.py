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
        worker.start_simulation(Settings(range_noise_std_m=0, sample_rate_hz=20))
        wait_for(app, lambda: len(received) >= 2)
        worker.stop_simulation()
        wait_for(app, lambda: states and states[-1] is False)
        before = len(port.writes)
        worker.start_simulation(Settings(range_noise_std_m=0))
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


def test_gui_extended_and_legacy_status_fields(app):
    from pc_sim.protocol import decode_status_payload
    window = MainWindow()
    try:
        window.connected = True
        window.on_status_received(decode_status_payload(bytes.fromhex(
            "12345678 80000000 00000001 ffffffff 00000007 01020304 ffffffff")))
        assert len(window.status_values) == 8
        assert window.status_values['ring_overflow_events'].text() == '7'
        assert window.status_values['rx_dropped_bytes'].text() == '16909060'
        assert window.status_values['recovery_discarded_bytes'].text() == '4294967295'
        # Legacy replies must replace old extended values with unsupported, not zero.
        window.on_status_received(decode_status_payload(bytes(16)))
        for key in ('ring_overflow_events', 'rx_dropped_bytes', 'recovery_discarded_bytes', 'stale_measurements'):
            assert window.status_values[key].text() == '미지원'
        assert window.status_values['crc_errors'].text() == '0'
    finally:
        window.connected = False
        window.close()


@pytest.mark.parametrize('interrupt_status', ['none', 'request', 'timeout'])
def test_worker_preserves_track_fragments_across_status_operations(app, monkeypatch, interrupt_status):
    from types import SimpleNamespace
    from pc_sim import gui_worker
    from pc_sim.protocol import encode_frame
    wire = encode_frame(7, bytes.fromhex(
        '00000001 00000064 00000064 447a0000 00000000 c1a00000 00000000 02 01 03 00'))
    reports, failures = [], []
    clock = [0.0]
    worker = SessionWorker('fake')
    worker.track_received.connect(lambda report, received_at: reports.append((report, received_at)))
    worker.status_failed.connect(failures.append)
    if interrupt_status == 'timeout':
        worker.request_status()

    class FragmentPort(FakeSerial):
        def __init__(self):
            super().__init__()
            self.in_waiting = 4096
            self.parts = [wire[:13], wire[13:]]

        def read(self, size):
            raw = self.parts.pop(0)
            if self.parts:
                if interrupt_status == 'request':
                    worker.request_status()
            else:
                clock[0] = 3.0 if interrupt_status == 'timeout' else 0.1
            return raw

    port = FragmentPort()
    monkeypatch.setattr(gui_worker, 'time', SimpleNamespace(monotonic=lambda: clock[0]))
    monkeypatch.setattr(worker, 'isInterruptionRequested', lambda: not port.parts)
    monkeypatch.setattr(worker, 'msleep', lambda _: None)
    worker._loop(port, None)
    assert len(reports) == 1
    assert reports[0][0].report_sequence == 1
    assert reports[0][1] == clock[0]
    assert len(failures) == (1 if interrupt_status == 'timeout' else 0)


def test_worker_demultiplexes_track_and_status_and_skips_bad_track(app, monkeypatch):
    from pc_sim.protocol import encode_frame
    good = encode_frame(7, bytes.fromhex(
        '00000009 00000064 00000064 447a0000 00000000 c1a00000 00000000 02 01 03 00'))
    corrupt = bytearray(good)
    corrupt[-1] ^= 1
    bad_payload = bytearray(good[3:-2])
    bad_payload[31] = 1
    raw = (bytes(corrupt) + encode_frame(7, bad_payload) + encode_frame(7, b'\0')
           + good + encode_frame(6, bytes(28)) + good)
    port = FakeSerial()
    port.in_waiting = len(raw)
    chunks = [raw]
    monkeypatch.setattr(port, 'read', lambda _: chunks.pop())
    worker = SessionWorker('fake')
    reports, stats, events = [], [], []
    worker.track_received.connect(lambda report, _: reports.append(report))
    worker.status_received.connect(stats.append)
    worker.event.connect(events.append)
    worker.request_status()
    monkeypatch.setattr(worker, 'isInterruptionRequested', lambda: not chunks)
    monkeypatch.setattr(worker, 'msleep', lambda _: None)
    worker._loop(port, None)
    assert [r.report_sequence for r in reports] == [9, 9]
    assert len(stats) == 1 and stats[0]['crc_errors'] == 0
    assert not worker.status_pending.is_set()
    assert sum('TRACK' in event for event in events) == 2


def test_gui_board_track_graph_status_and_stale_display(app, monkeypatch):
    import struct
    from types import SimpleNamespace
    from pc_sim import gui
    from pc_sim.protocol import encode_frame, decode_track_payload

    class TrackingSerial(StatusSerial):
        def __init__(self):
            super().__init__()
            self.sequence = 0

        def write(self, frame):
            result = super().write(frame)
            if frame[2] == 2:
                stamp, distance, _ = struct.unpack('>IHi', frame[3:-2])
                payload = struct.pack('>IIIffffBBBB', self.sequence, stamp, stamp,
                                      distance + 0.5, 0.25, -20, 0, 2, 1, 3, 0)
                self.sequence += 1
                self.buffer.extend(encode_frame(7, payload))
                self.in_waiting = len(self.buffer)
            return result

    port = TrackingSerial()
    monkeypatch.setattr(gui, 'SessionWorker', lambda data, console, parent:
        SessionWorker(data, console, parent, serial_factory=lambda *a, **kw: port))
    window = MainWindow()
    window.show()
    try:
        window.mode.setCurrentIndex(1)
        window.data_port.addItem('fake', 'fake')
        window.data_port.setCurrentIndex(window.data_port.count() - 1)
        window.connect_button.click()
        wait_for(app, lambda: window.connected)
        window.start_button.click()
        wait_for(app, lambda: window.track_monitor.latest is not None)
        window.draw_samples()
        assert window.track_values['state'].text() == 'CONFIRMED'
        assert len(window.estimated_curve.getData()[0]) >= 1
        assert len(window.estimated_point.getData()[0]) == 1
        window.request_status()
        wait_for(app, lambda: window.status_message.text() == '조회 성공')
        window.stop_button.click()
        wait_for(app, lambda: window.start_button.isEnabled() and not port.buffer)
        # UI freshness uses the worker's monotonic receive time, not simulation time.
        stale_at = window.track_monitor.received_at + 2.0
        monkeypatch.setattr(gui, 'time', SimpleNamespace(monotonic=lambda: stale_at))
        window.draw_samples()
        assert 'STALE' in window.track_message.text()
        assert window.estimated_point.getData()[0] is None
        assert window.track_values['state'].text() == 'CONFIRMED'
        assert len(window.estimated_curve.getData()[0]) >= 1  # Historical evidence stays visible.
        report = decode_track_payload(struct.pack(
            '>IIIffffBBBB', port.sequence, 999, 0, 0, 0, 0, 0, 0, 4, 1, 0))
        window.on_track_received(report, time.monotonic())
        assert window.track_values['range_m'].text() == '—'
        assert window.track_values['state'].text() == 'EMPTY'
        window.start_button.click()
        assert window.track_monitor.latest is None
        assert window.estimated_curve.getData()[0] is None
    finally:
        window.close()
        wait_for(app, lambda: window.worker is None)
    assert port.closed


def test_gui_track_tab_and_preview_have_no_fabricated_board_estimate(app):
    window = MainWindow()
    window.resize(1000, 650)
    window.show()
    try:
        window.control_tabs.setCurrentIndex(3)
        app.processEvents()
        window.draw_samples()
        assert window.control_tabs.tabText(3) == 'TRACK'
        assert window.track_values['state'].text() == '—'
        assert window.estimated_point.getData()[0] is None
        assert window.height() <= 650
    finally:
        window.close()


def test_worker_records_preview_and_closes_csv(app, tmp_path):
    import csv
    import json
    path = tmp_path / 'preview'
    worker = SessionWorker()
    samples = []
    worker.sample.connect(samples.append)
    worker.start_simulation(Settings(duration_s=0.15), path)
    worker.start()
    try:
        wait_for(app, lambda: len(samples) >= 1)
    finally:
        worker.requestInterruption()
        assert worker.wait(2000)
    assert json.loads((path / 'metadata.json').read_text())['closed']
    with (path / 'samples.csv').open() as stream:
        assert len(list(csv.DictReader(stream))) >= 1
    with (path / 'reports.csv').open() as stream:
        assert list(csv.DictReader(stream)) == []
