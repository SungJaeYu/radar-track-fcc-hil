"""
protocol.py — 바이너리 프레임 코덱 + FrameParser 상태머신

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
프레임 포맷  (C 파서와 바이트 단위로 정확히 일치시킬 것)
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

오프셋   크기   필드      설명
──────   ────   ──────    ────────────────────────────────────────────
  0       1     STX       0x7E  (프레임 시작 마커, 동기화 바이트)
  1       1     LEN       PAYLOAD 바이트 수 (0–255)
  2       1     TYPE      메시지 타입  →  MSG_* 상수 참조
  3      LEN    PAYLOAD   가변 길이 페이로드
 3+LEN    2     CRC16     CRC16-CCITT(XModem), big-endian
                          계산 범위: [LEN byte, TYPE byte, PAYLOAD...]
                          (STX는 CRC 범위에서 제외)

총 프레임 크기: 5 + LEN 바이트

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
메시지 타입
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

MSG_MEAS  = 0x01   PC → STM32   레이더 측정값
MSG_TRACK = 0x02   STM32 → PC   트랙 상태
MSG_CTRL  = 0x03   양방향       제어 명령

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
페이로드 레이아웃  (Python struct, little-endian '<')
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

MSG_MEAS  20바이트:  <I f f f f>
    uint32  timestamp_ms
    float   range_m         (0 ~ 50 000 m)
    float   azimuth_rad     (-π ~ +π)
    float   elevation_rad   (-π/2 ~ +π/2)
    float   doppler_mps     (- ~ +)

MSG_TRACK 21바이트:  <I B f f f f>
    uint32  timestamp_ms
    uint8   track_id        (0–255)
    float   x_m             (직교 좌표 동쪽)
    float   y_m             (직교 좌표 북쪽)
    float   vx_mps
    float   vy_mps

MSG_CTRL   1바이트:  <B>
    uint8   cmd             (CTRL_* 상수 참조)

⚠️  C 측 주의:
    Python '<' struct는 패딩 없음. C 구조체에는 반드시
    __attribute__((packed)) 또는 #pragma pack(1) 을 붙일 것.
    특히 MSG_TRACK의 uint8_t 앞에 uint32_t가 오는 레이아웃은
    패킹 없이는 3바이트 패딩이 삽입됨.

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
CRC16-CCITT (XModem variant)
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

poly   = 0x1021
init   = 0x0000
refin  = False
refout = False
xorout = 0x0000

검증값: crc16_ccitt(b"123456789") == 0x31C3
"""

import enum
import struct
from dataclasses import dataclass

# ── 프레임 상수 ────────────────────────────────────────────────────────────────

STX = 0x7E

MSG_MEAS  = 0x01
MSG_TRACK = 0x02
MSG_CTRL  = 0x03

CTRL_START = 0x01
CTRL_STOP  = 0x02
CTRL_RESET = 0x03

# struct 포맷 문자열 (C 측 레이아웃과 1:1 대응)
_FMT_MEAS  = "<I f f f f"    # timestamp_ms, range_m, az_rad, el_rad, doppler_mps
_FMT_TRACK = "<I B f f f f"  # timestamp_ms, track_id, x_m, y_m, vx_mps, vy_mps
_FMT_CTRL  = "<B"            # cmd

MEAS_SIZE  = struct.calcsize(_FMT_MEAS)   # 20
TRACK_SIZE = struct.calcsize(_FMT_TRACK)  # 21
CTRL_SIZE  = struct.calcsize(_FMT_CTRL)   # 1


# ── CRC16-CCITT ────────────────────────────────────────────────────────────────

def crc16_ccitt(data: bytes) -> int:
    """CRC16-CCITT (XModem): poly=0x1021, init=0x0000, 반사 없음."""
    crc = 0x0000
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) if crc & 0x8000 else (crc << 1)
            crc &= 0xFFFF
    return crc


# ── 프레임 인코더 ──────────────────────────────────────────────────────────────

def encode_frame(msg_type: int, payload: bytes) -> bytes:
    """(msg_type, payload) → 완성된 바이트 프레임."""
    if len(payload) > 255:
        raise ValueError(f"payload too long: {len(payload)} > 255")
    length = len(payload)
    crc = crc16_ccitt(bytes([length, msg_type]) + payload)
    return bytes([STX, length, msg_type]) + payload + struct.pack(">H", crc)


# ── 페이로드 데이터클래스 ──────────────────────────────────────────────────────

@dataclass
class MeasPayload:
    timestamp_ms:   int
    range_m:        float
    azimuth_rad:    float
    elevation_rad:  float
    doppler_mps:    float

    def encode(self) -> bytes:
        return struct.pack(_FMT_MEAS, self.timestamp_ms, self.range_m,
                           self.azimuth_rad, self.elevation_rad, self.doppler_mps)

    @classmethod
    def decode(cls, data: bytes) -> "MeasPayload":
        t, r, az, el, dop = struct.unpack(_FMT_MEAS, data)
        return cls(t, r, az, el, dop)


@dataclass
class TrackPayload:
    timestamp_ms: int
    track_id:     int
    x_m:          float
    y_m:          float
    vx_mps:       float
    vy_mps:       float

    def encode(self) -> bytes:
        return struct.pack(_FMT_TRACK, self.timestamp_ms, self.track_id,
                           self.x_m, self.y_m, self.vx_mps, self.vy_mps)

    @classmethod
    def decode(cls, data: bytes) -> "TrackPayload":
        t, tid, x, y, vx, vy = struct.unpack(_FMT_TRACK, data)
        return cls(t, tid, x, y, vx, vy)


@dataclass
class CtrlPayload:
    cmd: int

    def encode(self) -> bytes:
        return struct.pack(_FMT_CTRL, self.cmd)

    @classmethod
    def decode(cls, data: bytes) -> "CtrlPayload":
        (cmd,) = struct.unpack(_FMT_CTRL, data)
        return cls(cmd)


# ── FrameParser 상태머신 ───────────────────────────────────────────────────────

# 파서가 정상 LEN으로 허용하는 최대 페이로드 크기.
# 현재 최대 페이로드 = TRACK 21바이트. 64로 여유를 둠.
# 이 값 초과 시 해당 STX는 가비지로 간주하고 즉시 재동기화.
MAX_PAYLOAD = 64


class _St(enum.Enum):
    WAIT_STX     = 0
    WAIT_LEN     = 1
    WAIT_TYPE    = 2
    WAIT_PAYLOAD = 3
    WAIT_CRC_H   = 4
    WAIT_CRC_L   = 5


class FrameParser:
    """
    바이트 스트림 → (msg_type, payload) 상태머신.

    feed()에 임의 크기 청크를 넣으면 완성된 프레임을 yield한다.
    가비지 바이트: STX(0x7E) 이전 바이트는 조용히 버림.
    LEN > MAX_PAYLOAD: 가비지로 간주, 즉시 재동기화 (다음 STX 탐색).
    CRC 불일치: 해당 프레임을 버리고 다음 STX로 재동기화.
    """

    def __init__(self) -> None:
        self._st    = _St.WAIT_STX
        self._len   = 0
        self._type  = 0
        self._buf   = bytearray()
        self._crc_h = 0

    def reset(self) -> None:
        self._st = _St.WAIT_STX
        self._buf.clear()

    def feed(self, data: bytes):
        """bytes를 입력받아 완성 프레임 (msg_type, payload)을 yield."""
        for byte in data:
            result = self._step(byte)
            if result is not None:
                yield result

    def _step(self, b: int):
        st = self._st

        if st is _St.WAIT_STX:
            if b == STX:
                self._st = _St.WAIT_LEN

        elif st is _St.WAIT_LEN:
            if b > MAX_PAYLOAD:
                # 비정상적으로 큰 LEN → 이 STX는 가비지, 재동기화
                self._st = _St.WAIT_STX
            else:
                self._len = b
                self._st  = _St.WAIT_TYPE

        elif st is _St.WAIT_TYPE:
            self._type = b
            self._buf  = bytearray()
            self._st   = _St.WAIT_PAYLOAD if self._len > 0 else _St.WAIT_CRC_H

        elif st is _St.WAIT_PAYLOAD:
            self._buf.append(b)
            if len(self._buf) == self._len:
                self._st = _St.WAIT_CRC_H

        elif st is _St.WAIT_CRC_H:
            self._crc_h = b
            self._st    = _St.WAIT_CRC_L

        elif st is _St.WAIT_CRC_L:
            received = (self._crc_h << 8) | b
            expected = crc16_ccitt(bytes([self._len, self._type]) + bytes(self._buf))
            self._st = _St.WAIT_STX
            if received == expected:
                return (self._type, bytes(self._buf))
            # CRC 불일치 → 버리고 재동기화

        return None
