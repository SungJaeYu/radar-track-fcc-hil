"""Serial ownership and simulation pacing live outside the GUI thread."""
from contextlib import ExitStack
import codecs
import queue
import random
import time
from threading import Event

from PySide6.QtCore import QThread, Signal
import serial

from .gui_model import make_sample
from .frame_parser import FrameParser
from .protocol import (build_init_frame, encode_frame, decode_status_payload,
                       FRAME_TYPE_STATUS_REQUEST, FRAME_TYPE_STATUS_REPORT)


class SessionWorker(QThread):
    status_received = Signal(object)
    status_failed = Signal(str)
    STATUS_TIMEOUT_S = 2.0

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
        self.status_pending = Event()

    def start_simulation(self, settings):
        self.commands.put(('start', settings))

    def stop_simulation(self):
        self.commands.put(('stop', None))

    def request_status(self):
        # Called by the GUI thread; all serial I/O stays in run().
        if not self.data_port or self.status_pending.is_set():
            return False
        self.status_pending.set()
        self.commands.put(('status', None))
        return True

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
            if self.status_pending.is_set():
                self.status_pending.clear()
                self.status_failed.emit('연결 종료로 STATUS 조회가 취소되었습니다.')
            self.running_changed.emit(False)

    @staticmethod
    def _write(data, frame):
        # A successful write is not an acknowledgement from the board.
        if data is not None and data.write(frame) != len(frame):
            raise OSError('UART 프레임을 모두 쓰지 못했습니다.')

    def _loop(self, data, console):
        settings = None
        parser = FrameParser()
        status_deadline = None
        decoder = codecs.getincrementaldecoder('utf-8')(errors='replace')
        while not self.isInterruptionRequested():
            try:
                command, value = self.commands.get_nowait()
            except queue.Empty:
                command = None
            if command == 'status':
                # Reset assembly only; never flush the shared serial input buffer.
                parser.reset()
                status_deadline = time.monotonic() + self.STATUS_TIMEOUT_S
                self._write(data, encode_frame(FRAME_TYPE_STATUS_REQUEST, b"\x00"))
            elif command == 'stop':
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

            # Nonblocking bounded reads keep simulation pacing and stop commands responsive.
            if data is not None:
                raw = data.read(min(data.in_waiting, 4096))
                if status_deadline is not None and time.monotonic() >= status_deadline:
                    status_deadline = None
                    parser.reset()
                    self.status_pending.clear()
                    self.status_failed.emit('STATUS 응답 시간 초과')
                for frame in parser.feed(raw):
                    if status_deadline is None or frame.frame_type != FRAME_TYPE_STATUS_REPORT:
                        continue
                    try:
                        stats = decode_status_payload(frame.payload)
                    except ValueError:
                        continue
                    status_deadline = None
                    self.status_pending.clear()
                    self.status_received.emit(stats)

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
