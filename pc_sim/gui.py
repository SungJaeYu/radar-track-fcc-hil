"""Desktop HIL console. Launch with python -m pc_sim.gui."""
from collections import deque
import math
import sys

from PySide6.QtCore import Qt, QTimer
from PySide6.QtGui import QTextCursor
from PySide6.QtWidgets import (
    QApplication, QComboBox, QDoubleSpinBox, QFormLayout, QFrame,
    QHBoxLayout, QLabel, QMainWindow, QPlainTextEdit, QPushButton,
    QSplitter, QTabWidget, QVBoxLayout, QWidget,
)
import pyqtgraph as pg
from serial.tools import list_ports

from .gui_model import Settings
from .gui_worker import SessionWorker

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
QTabBar::tab { background: #1a2b39; padding: 8px 18px; }
QTabBar::tab:selected { background: #305064; }
QSplitter::handle { background: #293c4c; }
'''


class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.worker = None
        self.closing = False
        self.connected = False
        self.samples = deque(maxlen=2000)
        self.sample_count = 0
        self.setWindowTitle('Radar Lab · HIL Console')
        self.resize(1280, 850)
        self.setMinimumSize(1100, 820)
        self.setStyleSheet(STYLE)
        pg.setConfigOptions(antialias=True, background='#101820', foreground='#9db3c2')
        root = QWidget()
        self.setCentralWidget(root)
        layout = QVBoxLayout(root)
        layout.setContentsMargins(22, 18, 22, 18)
        title = QLabel('RADAR LAB  /  HIL CONSOLE')
        title.setObjectName('title')
        layout.addWidget(title)
        subtitle = QLabel('단일표적 시뮬레이터  ·  115200 baud / 8N1  ·  PC 생성값과 보드 로그를 구분합니다')
        subtitle.setObjectName('subtitle')
        layout.addWidget(subtitle)

        body = QHBoxLayout()
        layout.addLayout(body, 1)
        panel = QFrame()
        panel.setObjectName('panel')
        panel.setFixedWidth(295)
        controls = QVBoxLayout(panel)
        controls.setContentsMargins(14, 16, 14, 16)
        body.addWidget(panel)
        controls.addWidget(QLabel('01  연결'))
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
        controls.addSpacing(18)
        controls.addWidget(QLabel('02  단일표적 설정'))
        self.settings_form = QWidget()
        fields = QFormLayout(self.settings_form)
        fields.setContentsMargins(0, 0, 0, 0)
        self.inputs = {}
        definitions = [
            ('range_m', '초기 거리 (m)', 1, 65535, 1000, 1),
            ('rate_mps', '거리 변화율 (m/s)', -1000, 1000, -20, 1),
            ('azimuth_deg', '방위각 (°)', -180, 180, 15, 1),
            ('noise_m', '거리 잡음 σ (m)', 0, 1000, 5, 1),
            ('hz', '송신 주기 (Hz)', 1, 50, 10, 0),
            ('duration_s', '실행 시간 (s)', 1, 3600, 30, 0),
        ]
        for key, label, low, high, default, decimals in definitions:
            field = QDoubleSpinBox()
            field.setRange(low, high)
            field.setDecimals(decimals)
            field.setValue(default)
            self.inputs[key] = field
            fields.addRow(label, field)
        controls.addWidget(self.settings_form)
        note = QLabel('음수 변화율 = 접근\n방위각: 북쪽 0°, 동쪽 +90°\n시작할 때 INIT을 자동 전송합니다.')
        note.setWordWrap(True)
        note.setObjectName('subtitle')
        controls.addWidget(note)
        self.start_button = QPushButton('▶  시작')
        self.start_button.setObjectName('primary')
        self.start_button.clicked.connect(self.start_simulation)
        self.stop_button = QPushButton('■  송신 중지')
        self.stop_button.clicked.connect(self.stop_simulation)
        controls.addWidget(self.start_button)
        controls.addWidget(self.stop_button)
        controls.addStretch()

        right = QVBoxLayout()
        body.addLayout(right, 1)
        self.metrics = QLabel('대기  ·  생성/송신 0개  ·  경과 0.0 s')
        right.addWidget(self.metrics)
        split = QSplitter(Qt.Orientation.Vertical)
        right.addWidget(split, 1)
        plots = QSplitter(Qt.Orientation.Horizontal)
        split.addWidget(plots)
        self.map_plot = pg.PlotWidget(title='표적 위치 · PC 생성값')
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
        plots.addWidget(self.map_plot)
        self.range_plot = pg.PlotWidget(title='거리 이력 · PC 생성값')
        self.range_plot.setLabel('bottom', '시간', units='s')
        self.range_plot.setLabel('left', '거리', units='m')
        self.range_plot.showGrid(x=True, y=True, alpha=0.2)
        self.range_plot.addLegend(offset=(10, 10))
        self.truth_curve = self.range_plot.plot(pen=pg.mkPen('#3ce0b1', width=2), name='실제값 (합성)')
        self.measured_curve = self.range_plot.plot(pen=pg.mkPen('#f4bf65', width=1), name='측정값 (PC)')
        plots.addWidget(self.range_plot)
        plots.setSizes([400, 460])
        tabs = QTabWidget()
        self.console_text = QPlainTextEdit()
        self.event_text = QPlainTextEdit()
        for widget in (self.console_text, self.event_text):
            widget.setReadOnly(True)
            widget.setMaximumBlockCount(2000)
        tabs.addTab(self.console_text, '보드 콘솔 · ST-Link')
        tabs.addTab(self.event_text, '송신 / 실행 로그')
        split.addWidget(tabs)
        split.setSizes([440, 240])
        self.board_note = QLabel('보드 추정값 그래프는 아직 없습니다. 수신·처리 여부는 실제 콘솔 로그로 확인하세요.')
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
        self.console_text.clear()
        if not self.worker.console_port:
            self.console_text.setPlainText('콘솔 포트가 연결되지 않았습니다.\n보드 수신·추적 결과를 확인하려면 ST-Link 콘솔을 선택하세요.\n')
        self.update_controls()

    def start_simulation(self):
        if not self.connected:
            return
        settings = Settings(**{key: field.value() for key, field in self.inputs.items()})
        settings.validate()
        self.samples.clear()
        self.sample_count = 0
        for curve in (self.truth_curve, self.measured_curve, self.truth_point, self.measured_points):
            curve.setData([], [])
        extent = settings.range_m * 1.2
        self.map_plot.setRange(xRange=(-extent, extent), yRange=(-extent, extent), padding=0)
        self.start_button.setEnabled(False)
        self.settings_form.setEnabled(False)
        self.worker.start_simulation(settings)

    def stop_simulation(self):
        if self.worker:
            self.stop_button.setEnabled(False)
            self.worker.stop_simulation()

    def on_running(self, running):
        self.start_button.setEnabled(self.connected and not running)
        self.stop_button.setEnabled(self.connected and running)
        self.settings_form.setEnabled(not running)
        self.statusBar().showMessage('실행 중' if running else '송신 대기 / 중지')

    def on_sample(self, sample):
        self.samples.append(sample)
        self.sample_count += 1
        verb = 'UART write' if self.mode.currentIndex() else '미리보기 생성'
        self.metrics.setText(f'{verb}  {self.sample_count}개  ·  {sample.timestamp_ms / 1000:.1f} s'
                             f'  ·  실제 {sample.truth_m:.1f} m  ·  측정 {sample.measured_m} m')
        self.event_text.appendPlainText(f'{sample.timestamp_ms:>7} ms  {verb}  {sample.frame.hex(" ")}')

    def draw_samples(self):
        if not self.samples:
            return
        values = list(self.samples)
        times = [s.timestamp_ms / 1000 for s in values]
        self.truth_curve.setData(times, [s.truth_m for s in values])
        self.measured_curve.setData(times, [s.measured_m for s in values])
        last = values[-1]
        self.truth_point.setData([last.truth_m * math.sin(last.azimuth_rad)],
                                 [last.truth_m * math.cos(last.azimuth_rad)])
        trail = values[-100:]
        self.measured_points.setData([s.measured_m * math.sin(s.azimuth_rad) for s in trail],
                                     [s.measured_m * math.cos(s.azimuth_rad) for s in trail])

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
        self.connect_button.setEnabled(False)
        self.start_button.setEnabled(False)
        self.stop_button.setEnabled(False)
        self.connection_label.setText('연결 해제 중…')
        self.worker.requestInterruption()

    def on_disconnected(self):
        self.worker.deleteLater()
        self.worker = None
        self.connected = False
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
