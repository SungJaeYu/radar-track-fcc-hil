"""Desktop HIL console. Launch with python -m pc_sim.gui."""
from collections import deque
import math
from datetime import datetime
from pathlib import Path
import sys
import time

from PySide6.QtCore import Qt, QTimer
from PySide6.QtGui import QTextCursor
from PySide6.QtWidgets import (
    QApplication, QCheckBox, QFileDialog, QComboBox, QDoubleSpinBox, QFormLayout, QFrame,
    QHBoxLayout, QLabel, QMainWindow, QPlainTextEdit, QPushButton,
    QSplitter, QTabWidget, QVBoxLayout, QWidget, QScrollArea, QLayout, QSizePolicy,
)
import pyqtgraph as pg
from serial.tools import list_ports

from .gui_model import Settings, TrackMonitor
from .gui_worker import SessionWorker
from .recording import session_path
from .protocol import STATUS_FIELDS

STYLE = '''
QWidget { background: #101820; color: #dce8ef; font-size: 13px; }
QLabel#title { font-size: 25px; font-weight: 700; color: #ffffff; }
QLabel#subtitle { color: #8fa8b9; }
QFrame#panel { background: #17232e; border: 1px solid #293c4c; border-radius: 8px; }
QComboBox, QDoubleSpinBox { background: #0d151e; border: 1px solid #365064;
    border-radius: 4px; padding: 6px; min-height: 22px; }
QPushButton { background: #253d50; border: 1px solid #36546a;
    border-radius: 5px; padding: 9px 14px; font-weight: 600; }
QPushButton:hover { background: #34576e; }
QPushButton#primary { background: #147e75; border: 1px solid #30c9b8; }
QPushButton#primary:disabled { background: #20413e; border-color: #305550; color: #809390; }
QPushButton:disabled { color: #647788; background: #1b2935; border-color: #293c4c; }
QPlainTextEdit { background: #0c131b; border: 1px solid #293c4c;
    font-family: Menlo, monospace; font-size: 12px; }
QTabBar::tab { background: #1a2b39; padding: 8px 10px; }
QTabBar::tab:selected { background: #305064; }
QSplitter::handle { background: #293c4c; }
'''


class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.worker = None
        self.closing = False
        self.connected = False
        self.status_pending = False
        self.samples = deque(maxlen=2000)
        self.sample_count = 0
        self.track_monitor = TrackMonitor()
        self.setWindowTitle('Radar Lab · HIL Console')
        self.resize(1280, 850)
        self.setMinimumSize(950, 600)
        screen = QApplication.primaryScreen().availableGeometry()
        self.resize(min(1280, screen.width() - 40), min(850, screen.height() - 80))
        self.setStyleSheet(STYLE)
        pg.setConfigOptions(antialias=True, background='#101820', foreground='#9db3c2')
        root = QWidget()
        self.setCentralWidget(root)
        layout = QVBoxLayout(root)
        layout.setContentsMargins(16, 14, 16, 14)
        title = QLabel('RADAR LAB  /  HIL CONSOLE')
        title.setObjectName('title')
        layout.addWidget(title)
        subtitle = QLabel('단일표적 시뮬레이터  ·  115200 baud / 8N1  ·  PC 합성값 / 보드 추정값 / 진단 로그')
        subtitle.setObjectName('subtitle')
        subtitle.setWordWrap(True)
        layout.addWidget(subtitle)

        body = QHBoxLayout()
        layout.addLayout(body, 1)
        panel = QFrame()
        panel.setObjectName('panel')
        panel.setFixedWidth(310)
        panel_layout = QVBoxLayout(panel)
        panel_layout.setContentsMargins(10, 12, 10, 12)
        body.addWidget(panel)
        self.control_tabs = QTabWidget()
        panel_layout.addWidget(self.control_tabs, 1)

        def tab_page(title):
            scroll = QScrollArea()
            scroll.setWidgetResizable(True)
            scroll.setFrameShape(QFrame.Shape.NoFrame)
            scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
            content = QWidget()
            page_layout = QVBoxLayout(content)
            page_layout.setContentsMargins(8, 12, 8, 12)
            page_layout.setSizeConstraint(QLayout.SizeConstraint.SetMinimumSize)
            scroll.setWidget(content)
            self.control_tabs.addTab(scroll, title)
            return scroll, page_layout

        _, controls = tab_page('연결')
        self.mode = QComboBox()
        self.mode.addItems(['미리보기 · 보드 연결 없음', '보드 UART · 실제 송신'])
        controls.addWidget(self.mode)
        self.data_port = QComboBox()
        self.console_port = QComboBox()
        for label, combo in (('데이터 포트 · USB-UART', self.data_port),
                             ('콘솔 포트 · ST-Link', self.console_port)):
            combo.setSizeAdjustPolicy(QComboBox.SizeAdjustPolicy.AdjustToMinimumContentsLengthWithIcon)
            combo.setMinimumContentsLength(16)
            controls.addWidget(QLabel(label))
            controls.addWidget(combo)
        self.refresh_button = QPushButton('포트 새로고침')
        self.refresh_button.clicked.connect(self.refresh_ports)
        controls.addWidget(self.refresh_button)
        self.connect_button = QPushButton('미리보기 연결')
        self.connect_button.clicked.connect(self.toggle_connection)
        controls.addWidget(self.connect_button)
        self.connection_label = QLabel('연결되지 않음')
        self.connection_label.setWordWrap(True)
        controls.addWidget(self.connection_label)
        controls.addStretch()
        self.controls_scroll, controls = tab_page('설정')
        scope_note = QLabel('단일표적 · 고정 방위각 / 일정 거리 변화율')
        scope_note.setWordWrap(True)
        controls.addWidget(scope_note)
        self.record_directory = Path.cwd() / 'runs'
        self.record_checkbox = QCheckBox('CSV 기록 (실행별 새 폴더)')
        controls.addWidget(self.record_checkbox)
        self.record_button = QPushButton('저장 폴더 선택')
        self.record_button.clicked.connect(self.choose_record_directory)
        controls.addWidget(self.record_button)
        self.record_label = QLabel(str(self.record_directory))
        self.record_label.setWordWrap(True)
        controls.addWidget(self.record_label)
        self.settings_form = QWidget()
        fields = QFormLayout(self.settings_form)
        fields.setContentsMargins(0, 0, 0, 0)
        fields.setFieldGrowthPolicy(QFormLayout.FieldGrowthPolicy.AllNonFixedFieldsGrow)
        fields.setRowWrapPolicy(QFormLayout.RowWrapPolicy.WrapAllRows)
        fields.setVerticalSpacing(10)
        fields.setSizeConstraint(QLayout.SizeConstraint.SetMinimumSize)
        self.inputs = {}
        definitions = [
            ('initial_range_m', '초기 거리 (m)', 1, 65535, 1000, 1),
            ('range_rate_mps', '거리 변화율 (m/s)', -1000, 1000, -20, 1),
            ('azimuth_deg', '방위각 (°)', -180, 180, 15, 1),
            ('range_noise_std_m', '거리 잡음 σ (m)', 0, 1000, 5, 1),
            ('sample_rate_hz', '송신 주기 (Hz)', 1, 50, 10, 0),
            ('duration_s', '실행 시간 (s)', 1, 3600, 30, 0),
        ]
        for key, label, low, high, default, decimals in definitions:
            field = QDoubleSpinBox()
            field.setRange(low, high)
            field.setDecimals(decimals)
            field.setValue(default)
            field.setMinimumHeight(max(38, field.sizeHint().height()))
            field.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Fixed)
            self.inputs[key] = field
            fields.addRow(label, field)
        controls.addWidget(self.settings_form)
        note = QLabel('음수 변화율 = 접근\n방위각: 북쪽 0°, 동쪽 +90°\n시작할 때 INIT을 자동 전송합니다.')
        note.setWordWrap(True)
        note.setObjectName('subtitle')
        controls.addWidget(note)
        _, status_layout = tab_page('STATUS')
        self.status_button = QPushButton('STATUS 조회')
        self.status_button.clicked.connect(self.request_status)
        status_layout.addWidget(self.status_button)
        self.status_message = QLabel('보드 연결 후 조회할 수 있습니다.')
        self.status_message.setWordWrap(True)
        status_layout.addWidget(self.status_message)
        status_form = QFormLayout()
        self.status_values = {}
        for key, _, label in STATUS_FIELDS:
            value = QLabel('—')
            value.setTextInteractionFlags(Qt.TextInteractionFlag.TextSelectableByMouse)
            self.status_values[key] = value
            status_form.addRow(label, value)
        status_layout.addLayout(status_form)
        self.status_updated = QLabel('마지막 성공 조회: —')
        self.status_updated.setWordWrap(True)
        status_layout.addWidget(self.status_updated)
        note = QLabel('보드 RX 누적 통계입니다. BIT 결과가 아니며, 조회 실패 시 이전 값을 유지합니다.')
        note.setWordWrap(True)
        status_layout.addWidget(note)
        status_layout.addStretch()

        self.track_scroll, track_layout = tab_page('TRACK')
        self.track_message = QLabel('보드 TRACK 수신 대기')
        self.track_message.setWordWrap(True)
        track_layout.addWidget(self.track_message)
        track_form = QFormLayout()
        self.track_values = {}
        for key, label in (
            ('report_sequence', '보고 순번'), ('state', '마지막 보드 상태'),
            ('result', '처리 결과'), ('input_timestamp_ms', '입력 시각 (ms)'),
            ('estimate_timestamp_ms', '추정 시각 (ms)'), ('range_m', '거리 (m)'),
            ('azimuth_rad', '방위각 (rad)'), ('range_rate_mps', '거리 변화율 (m/s)'),
            ('azimuth_rate_radps', '방위각 변화율 (rad/s)'),
            ('sequence_gaps', '관측 순번 공백'), ('discontinuities', '순번 역행 / 불연속'),
        ):
            value = QLabel('—')
            value.setTextInteractionFlags(Qt.TextInteractionFlag.TextSelectableByMouse)
            self.track_values[key] = value
            track_form.addRow(label, value)
        track_layout.addLayout(track_form)
        note = QLabel('STALE: 새 TRACK 수신 없이 2초 경과. 보드 LOST와 다릅니다.\n'
                      '순번 공백은 유실 위치·원인을 확정하지 않습니다.\n'
                      '거리 이력은 PC 측정 시각과 일치하는 추정 시각만 표시합니다.')
        note.setWordWrap(True)
        note.setObjectName('subtitle')
        track_layout.addWidget(note)
        track_layout.addStretch()

        self.start_button = QPushButton('▶  시작')
        self.start_button.setObjectName('primary')
        self.start_button.clicked.connect(self.start_simulation)
        self.stop_button = QPushButton('■  송신 중지')
        self.stop_button.clicked.connect(self.stop_simulation)
        controls.addStretch()
        panel_layout.addWidget(self.start_button)
        panel_layout.addWidget(self.stop_button)

        right = QVBoxLayout()
        body.addLayout(right, 1)
        self.metrics = QLabel('대기  ·  생성/송신 0개  ·  경과 0.0 s')
        self.metrics.setWordWrap(True)
        right.addWidget(self.metrics)
        self.content_split = split = QSplitter(Qt.Orientation.Horizontal)
        split.setChildrenCollapsible(False)
        right.addWidget(split, 1)
        self.plots_split = plots = QSplitter(Qt.Orientation.Vertical)
        plots.setChildrenCollapsible(False)
        split.addWidget(plots)
        self.map_plot = pg.PlotWidget(title='표적 위치')
        self.map_plot.setLabel('bottom', '동쪽', units='m')
        self.map_plot.setLabel('left', '북쪽', units='m')
        self.map_plot.setAspectLocked(True)
        self.map_plot.showGrid(x=True, y=True, alpha=0.2)
        self.map_plot.addLegend(offset=(10, 10))
        self.map_plot.plot([0], [0], pen=None, symbol='t', symbolSize=14,
                           symbolBrush='#ffffff')
        self.truth_point = self.map_plot.plot([], [], pen=None, symbol='o', symbolSize=12,
                                              symbolBrush='#3ce0b1', name='실제값 (합성)')
        self.measured_points = self.map_plot.plot([], [], pen=None, symbol='+', symbolSize=10,
                                                  symbolPen='#f4bf65', name='측정값 (PC)')
        self.estimated_point = self.map_plot.plot([], [], pen=None, symbol='d', symbolSize=12,
                                                  symbolBrush='#75bfff', name='추정값 (보드)')
        plots.addWidget(self.map_plot)
        self.range_plot = pg.PlotWidget(title='거리 이력 · 센서 시각 기준')
        self.range_plot.setLabel('bottom', '시간', units='s')
        self.range_plot.setLabel('left', '거리', units='m')
        self.range_plot.showGrid(x=True, y=True, alpha=0.2)
        self.range_plot.addLegend(offset=(10, 10))
        self.truth_curve = self.range_plot.plot(pen=pg.mkPen('#3ce0b1', width=2), name='실제값 (합성)')
        self.measured_curve = self.range_plot.plot(pen=pg.mkPen('#f4bf65', width=1), name='측정값 (PC)')
        self.estimated_curve = self.range_plot.plot(pen=pg.mkPen('#75bfff', width=2),
                                                     connect='finite', name='추정값 (보드)')
        plots.addWidget(self.range_plot)
        plots.setSizes([360, 300])
        plots.setStretchFactor(0, 1)
        plots.setStretchFactor(1, 1)
        self.log_tabs = tabs = QTabWidget()
        self.console_text = QPlainTextEdit()
        self.event_text = QPlainTextEdit()
        for widget in (self.console_text, self.event_text):
            widget.setReadOnly(True)
            widget.setMaximumBlockCount(2000)
        tabs.addTab(self.console_text, '보드 콘솔 · ST-Link')
        tabs.addTab(self.event_text, '송신 / 실행 로그')
        split.addWidget(tabs)
        split.setSizes([460, 440])
        split.setStretchFactor(0, 1)
        split.setStretchFactor(1, 1)
        self.board_note = QLabel('보드 TRACK 수신 대기 · 미리보기에서는 보드 추정값을 생성하지 않습니다.')
        self.board_note.setWordWrap(True)
        self.board_note.setObjectName('subtitle')
        right.addWidget(self.board_note)
        self.mode.currentIndexChanged.connect(self.update_controls)
        self.refresh_ports()
        self.update_controls()
        self.plot_timer = QTimer(self)
        self.plot_timer.timeout.connect(self.draw_samples)
        self.plot_timer.start(100)

    def refresh_ports(self):
        ports = list(list_ports.comports())
        for combo in (self.data_port, self.console_port):
            selected = combo.currentData()
            combo.clear()
            combo.addItem('선택 안 함', None)
            for port in ports:
                combo.addItem(f'{port.device} · {port.description}', port.device)
            index = combo.findData(selected)
            combo.setCurrentIndex(max(0, index))

    def update_controls(self):
        busy = self.worker is not None
        board = self.mode.currentIndex() == 1
        self.mode.setEnabled(not busy)
        self.refresh_button.setEnabled(not busy)
        self.data_port.setEnabled(board and not busy)
        self.console_port.setEnabled(board and not busy)
        self.connect_button.setText('연결 해제' if busy else ('보드 연결' if board else '미리보기 연결'))
        self.connect_button.setEnabled(not busy or self.connected)
        self.start_button.setEnabled(self.connected)
        self.stop_button.setEnabled(False)
        self.status_button.setEnabled(self.connected and board and not self.status_pending)

    def toggle_connection(self):
        if self.worker is not None:
            self.disconnect()
            return
        board = self.mode.currentIndex() == 1
        data = self.data_port.currentData() if board else None
        console = self.console_port.currentData() if board else None
        if board and not data:
            self.show_error('데이터 포트를 선택하세요.')
            return
        if data and data == console:
            self.show_error('데이터와 콘솔에 같은 포트를 사용할 수 없습니다.')
            return
        self.worker = SessionWorker(data, console, self)
        self.worker.recording_started.connect(self.on_recording_started)
        self.worker.track_received.connect(self.on_track_received)
        self.worker.status_received.connect(self.on_status_received)
        self.worker.status_failed.connect(self.on_status_failed)
        self.worker.connected.connect(self.on_connected)
        self.worker.sample.connect(self.on_sample)
        self.worker.console.connect(self.append_console)
        self.worker.event.connect(self.event_text.appendPlainText)
        self.worker.error.connect(self.show_error)
        self.worker.running_changed.connect(self.on_running)
        self.worker.finished.connect(self.on_disconnected)
        self.connection_label.setText('연결 중…')
        self.update_controls()
        self.worker.start()

    def on_connected(self):
        self.connected = True
        board = self.mode.currentIndex() == 1
        self.connection_label.setText('보드 포트 연결됨' if board else '미리보기 준비됨 · UART 사용 안 함')
        self.event_text.appendPlainText(self.connection_label.text())
        self.status_pending = False
        for value in self.status_values.values():
            value.setText('—')
        self.status_updated.setText('마지막 성공 조회: —')
        self.status_message.setText('조회 대기' if board else '미리보기에서는 조회할 수 없습니다.')
        self.console_text.clear()
        self.reset_track_display()
        if not self.worker.console_port:
            self.console_text.setPlainText('콘솔 포트가 연결되지 않았습니다.\n진단 로그는 ST-Link 콘솔, 추정 결과는 데이터 포트의 TRACK으로 수신합니다.\n')
        self.update_controls()

    def request_status(self):
        if not self.connected or self.worker is None or self.status_pending:
            return
        if self.worker.request_status():
            self.status_pending = True
            self.status_button.setEnabled(False)
            self.status_message.setText('응답 대기 중…')

    def on_status_received(self, stats):
        if not self.connected:
            return
        self.status_pending = False
        for key, value in self.status_values.items():
            value.setText(str(stats[key]) if key in stats else "미지원")
        self.status_updated.setText('마지막 성공 조회: ' + datetime.now().strftime('%H:%M:%S'))
        self.status_message.setText('조회 성공')
        self.status_button.setEnabled(True)

    def on_status_failed(self, message):
        self.status_pending = False
        self.status_message.setText(message)
        self.status_button.setEnabled(self.connected and self.mode.currentIndex() == 1)

    def reset_track_display(self):
        self.track_monitor.reset()
        for value in self.track_values.values():
            value.setText('—')
        self.estimated_curve.setData([], [])
        self.estimated_point.setData([], [])
        self.draw_track()

    def on_track_received(self, report, received_at):
        if not self.connected or not self.track_monitor.receive(report, received_at):
            return
        self.track_values['report_sequence'].setText(str(report.report_sequence))
        self.track_values['state'].setText(report.state.name)
        self.track_values['result'].setText(report.result.name)
        self.track_values['input_timestamp_ms'].setText(
            str(report.input_timestamp_ms) if report.input_present else '—')
        self.track_values['estimate_timestamp_ms'].setText(
            str(report.estimate_timestamp_ms) if report.estimate_valid else '—')
        for key in ('range_m', 'azimuth_rad', 'range_rate_mps', 'azimuth_rate_radps'):
            self.track_values[key].setText(f'{getattr(report, key):.3f}' if report.estimate_valid else '—')
        self.track_values['sequence_gaps'].setText(str(self.track_monitor.sequence_gaps))
        self.track_values['discontinuities'].setText(str(self.track_monitor.discontinuities))
        self.draw_track()

    def draw_track(self):
        monitor = self.track_monitor
        report = monitor.latest
        stale = monitor.is_stale(time.monotonic())
        live = self.connected and not stale and report is not None and report.estimate_valid
        if live:
            self.estimated_point.setData([report.range_m * math.sin(report.azimuth_rad)],
                                         [report.range_m * math.cos(report.azimuth_rad)])
        else:
            self.estimated_point.setData([], [])
        times, ranges = monitor.range_history(self.samples)
        self.estimated_curve.setData(times, ranges)
        self.estimated_curve.setOpacity(1.0 if self.connected and not stale else 0.35)
        if report is None:
            message = ('보드 TRACK 수신 대기' if self.mode.currentIndex() == 1 else
                       '미리보기 · 보드 추정값 없음')
        else:
            age = max(0.0, time.monotonic() - monitor.received_at)
            freshness = ('연결 해제 · 이전 결과' if not self.connected else
                         'STALE · 오래된 결과' if stale else '수신 중')
            validity = '추정 유효' if report.estimate_valid else '추정 무효'
            message = f'{freshness} · #{report.report_sequence} · {age:.1f}초 전 · {validity}'
        self.track_message.setText(message)
        self.board_note.setText(message)

    def choose_record_directory(self):
        directory = QFileDialog.getExistingDirectory(self, 'CSV 저장 폴더', str(self.record_directory))
        if directory:
            self.record_directory = Path(directory)
            self.record_label.setText(directory)

    def on_recording_started(self, path):
        self.record_label.setText(path)
        self.event_text.appendPlainText('CSV 기록: ' + path)

    def start_simulation(self):
        if not self.connected:
            return
        settings = Settings(**{key: field.value() for key, field in self.inputs.items()})
        settings.validate()
        self.samples.clear()
        self.sample_count = 0
        self.reset_track_display()
        for curve in (self.truth_curve, self.measured_curve, self.truth_point, self.measured_points):
            curve.setData([], [])
        extent = settings.initial_range_m * 1.2
        self.map_plot.setRange(xRange=(-extent, extent), yRange=(-extent, extent), padding=0)
        self.start_button.setEnabled(False)
        self.settings_form.setEnabled(False)
        path = session_path(self.record_directory) if self.record_checkbox.isChecked() else None
        self.worker.start_simulation(settings, path)

    def stop_simulation(self):
        if self.worker:
            self.stop_button.setEnabled(False)
            self.worker.stop_simulation()

    def on_running(self, running):
        self.record_checkbox.setEnabled(not running)
        self.record_button.setEnabled(not running)
        self.start_button.setEnabled(self.connected and not running)
        self.stop_button.setEnabled(self.connected and running)
        self.settings_form.setEnabled(not running)
        self.statusBar().showMessage('실행 중' if running else '송신 대기 / 중지')

    def on_sample(self, sample):
        self.samples.append(sample)
        self.sample_count += 1
        verb = 'UART write' if self.mode.currentIndex() else '미리보기 생성'
        self.metrics.setText(f'{verb}  {self.sample_count}개  ·  {sample.timestamp_ms / 1000:.1f} s'
                             f'  ·  실제 {sample.truth_range_m:.1f} m  ·  측정 {sample.measured_range_m} m')
        self.event_text.appendPlainText(f'{sample.timestamp_ms:>7} ms  {verb}  {sample.frame.hex(" ")}')

    def draw_samples(self):
        # Freshness must still update when simulation is stopped or no samples exist.
        self.draw_track()
        if not self.samples:
            return
        values = list(self.samples)
        times = [s.timestamp_ms / 1000 for s in values]
        self.truth_curve.setData(times, [s.truth_range_m for s in values])
        self.measured_curve.setData(times, [s.measured_range_m for s in values])
        last = values[-1]
        self.truth_point.setData([last.truth_range_m * math.sin(last.azimuth_rad)],
                                 [last.truth_range_m * math.cos(last.azimuth_rad)])
        trail = values[-100:]
        self.measured_points.setData([s.measured_range_m * math.sin(s.azimuth_rad) for s in trail],
                                     [s.measured_range_m * math.cos(s.azimuth_rad) for s in trail])

    def append_console(self, text):
        cursor = self.console_text.textCursor()
        cursor.movePosition(QTextCursor.MoveOperation.End)
        cursor.insertText(text.replace('\r', ''))
        self.console_text.setTextCursor(cursor)
        self.console_text.ensureCursorVisible()

    def show_error(self, text):
        self.event_text.appendPlainText('오류: ' + text)
        self.statusBar().showMessage('오류: ' + text)

    def disconnect(self):
        self.connected = False
        self.draw_track()
        self.connect_button.setEnabled(False)
        self.start_button.setEnabled(False)
        self.stop_button.setEnabled(False)
        self.status_button.setEnabled(False)
        self.connection_label.setText('연결 해제 중…')
        self.worker.requestInterruption()

    def on_disconnected(self):
        self.worker.deleteLater()
        self.worker = None
        self.connected = False
        self.status_pending = False
        self.status_message.setText('연결 해제 · 표시된 값은 이전 조회 결과입니다.')
        self.connection_label.setText('연결되지 않음')
        self.settings_form.setEnabled(True)
        self.update_controls()
        if self.closing:
            self.close()

    def closeEvent(self, event):
        if self.worker is not None:
            self.closing = True
            self.disconnect()
            event.ignore()
        else:
            event.accept()


def main():
    app = QApplication(sys.argv)
    window = MainWindow()
    window.show()
    sys.exit(app.exec())


if __name__ == '__main__':
    main()
