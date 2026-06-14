"""
transport.py — 전송 계층 추상화

MockTransport  : 인메모리 루프백 (보드 없이 PC 단독 테스트용)
SerialTransport: pyserial 기반 실제 UART (STM32 연결용)

공통 인터페이스:
    send(data: bytes) -> None
    recv(n: int) -> bytes          # 최대 n바이트, 타임아웃 있으면 짧을 수 있음
    close() -> None
"""

import queue
import threading
from typing import Optional


class MockTransport:
    """
    인메모리 양방향 채널.
    send() 한 데이터는 recv()로 돌아온다 (루프백).
    PC-only 통합 테스트에 사용.
    """

    def __init__(self) -> None:
        self._q: queue.Queue[bytes] = queue.Queue()

    def send(self, data: bytes) -> None:
        self._q.put(data)

    def recv(self, n: int = 256, timeout: float = 1.0) -> bytes:
        """큐에서 꺼낸다. timeout 초 안에 데이터 없으면 빈 bytes 반환."""
        try:
            chunk = self._q.get(timeout=timeout)
            return chunk[:n]
        except queue.Empty:
            return b""

    def close(self) -> None:
        pass

    def inject(self, data: bytes) -> None:
        """테스트용: 외부에서 수신 버퍼에 직접 데이터 주입."""
        self._q.put(data)


class SerialTransport:
    """
    pyserial 기반 UART 전송.
    포트 예: /dev/ttyACM0, baudrate: 115200
    """

    def __init__(self, port: str, baudrate: int = 115200,
                 timeout: float = 1.0) -> None:
        try:
            import serial
        except ImportError:
            raise ImportError("pyserial 필요: pip install pyserial")

        self._ser = serial.Serial(port, baudrate=baudrate,
                                  timeout=timeout, write_timeout=2.0)

    def send(self, data: bytes) -> None:
        self._ser.write(data)
        self._ser.flush()

    def recv(self, n: int = 256) -> bytes:
        return self._ser.read(n)

    def close(self) -> None:
        if self._ser.is_open:
            self._ser.close()

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()
