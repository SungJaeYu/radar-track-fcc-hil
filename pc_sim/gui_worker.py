"""Serial ownership and simulation pacing live outside the GUI thread."""
from contextlib import ExitStack
import codecs
import queue
import random
import time

from PySide6.QtCore import QThread, Signal
import serial

from .gui_model import make_sample
from .protocol import build_init_frame


class SessionWorker(QThread):
    connected = Signal()
    sample = Signal(object)
    console = Signal(str)
    event = Signal(str)
    error = Signal(str)
    running_changed = Signal(bool)

    def __init__(self, data_port=None, console_port=None, parent=None, serial_factory=None):
        super().__init__(parent)
        self.data_port = data_port
        self.console_port = console_port
        self.serial_factory = serial_factory or serial.Serial
        self.commands = queue.Queue()

    def start_simulation(self, settings):
        self.commands.put(('start', settings))

    def stop_simulation(self):
        self.commands.put(('stop', None))

    def run(self):
        try:
            if self.console_port and not self.data_port:
                raise ValueError('콘솔 연결은 보드 모드에서만 가능합니다.')
            if self.data_port and self.data_port == self.console_port:
                raise ValueError('데이터와 콘솔은 서로 다른 포트를 선택하세요.')
            with ExitStack() as stack:
                data = (stack.enter_context(self.serial_factory(
                    self.data_port, 115200, timeout=0, write_timeout=0.5))
                    if self.data_port else None)
                console = (stack.enter_context(self.serial_factory(
                    self.console_port, 115200, timeout=0, write_timeout=0.5))
                    if self.console_port else None)
                self.connected.emit()
                self._loop(data, console)
        except Exception as error:
            self.error.emit(str(error))
        finally:
            self.running_changed.emit(False)

    @staticmethod
    def _write(data, frame):
        # A successful write is not an acknowledgement from the board.
        if data is not None and data.write(frame) != len(frame):
            raise OSError('UART 프레임을 모두 쓰지 못했습니다.')

    def _loop(self, data, console):
        settings = None
        decoder = codecs.getincrementaldecoder('utf-8')(errors='replace')
        while not self.isInterruptionRequested():
            try:
                command, value = self.commands.get_nowait()
            except queue.Empty:
                command = None
            if command == 'stop':
                settings = None
                self.running_changed.emit(False)
                self.event.emit('송신 중지 · 콘솔 모니터는 유지됩니다.')
            elif command == 'start':
                value.validate()
                self._write(data, build_init_frame())
                self.event.emit('INIT 전송' if data else 'INIT 생성 (미리보기)')
                settings = value
                rng = random.Random(settings.seed)
                started = next_sample = time.monotonic()
                self.running_changed.emit(True)

            if console is not None:
                raw = console.read(min(console.in_waiting, 4096))
                if raw:
                    self.console.emit(decoder.decode(raw))

            now = time.monotonic()
            if settings is not None and now >= next_sample:
                elapsed = now - started
                if elapsed >= settings.duration_s:
                    settings = None
                    self.running_changed.emit(False)
                    self.event.emit('설정한 실행 시간 완료 · 콘솔 모니터는 유지됩니다.')
                else:
                    try:
                        sample = make_sample(settings, round(elapsed * 1000), rng)
                    except ValueError as error:
                        settings = None
                        self.running_changed.emit(False)
                        self.error.emit(str(error))
                    else:
                        self._write(data, sample.frame)
                        self.sample.emit(sample)
                        # Never send catch-up bursts after a delayed write/read.
                        next_sample = time.monotonic() + 1 / settings.hz
            self.msleep(5)
