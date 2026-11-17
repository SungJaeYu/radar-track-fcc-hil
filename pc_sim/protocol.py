"""Shared UART framing, measurement encoding, and board report decoding."""
from dataclasses import dataclass
from enum import IntEnum, IntFlag
import math
import struct

STX = 0x7E
FRAME_TYPE_INIT = 0x01
FRAME_TYPE_MEAS_REPORT = 0x02
FRAME_TYPE_STATUS_REQUEST = 0x03
FRAME_TYPE_COMMAND = 0x04
FRAME_TYPE_RESET = 0x05
# Firmware diagnostic response decoded by the PC STATUS client.
FRAME_TYPE_STATUS_REPORT = 0x06
# Unsolicited measurement results from the board.
FRAME_TYPE_TRACK_REPORT = 0x07


def crc16(data):
    """CRC-16/IBM-3740: poly=0x1021, init=0xFFFF, no reflection or final XOR."""
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def encode_frame(frame_type, payload):
    """STX | LEN | TYPE | PAYLOAD | CRC_H | CRC_L; CRC covers LEN through payload."""
    if not 1 <= len(payload) <= 32:
        raise ValueError("payload length must be 1..32 bytes")
    body = bytes([len(payload), frame_type]) + payload
    return bytes([STX]) + body + crc16(body).to_bytes(2, "big")


def build_init_frame():
    """새 측정 시퀀스 시작. 현재 파서는 빈 payload를 거부하므로 1바이트를 넣는다."""
    return encode_frame(FRAME_TYPE_INIT, b"\x00")


def build_measurement_frame(timestamp_ms, range_m, azimuth_sc):
    """Encode int32 semicircle azimuth (radians = azimuth_sc * pi / 2**31)."""
    payload = struct.pack(
        ">IHi",
        timestamp_ms,
        range_m,
        azimuth_sc
    )

    return encode_frame(FRAME_TYPE_MEAS_REPORT, payload)


def build_measurement_frame_from_radians(timestamp_ms, range_m, azimuth_rad):
    """시나리오의 실수 거리·라디안을 현재 wire 단위로 변환한다."""
    if not math.isfinite(range_m) or not math.isfinite(azimuth_rad):
        raise ValueError("range and azimuth must be finite")
    distance = round(range_m)
    if not 1 <= distance <= 65535:
        raise ValueError("rounded range must be 1..65535 metres")
    # +pi는 -pi와 같은 방향이다. 반올림 후에도 signed int32로 감싼다.
    wrapped = (azimuth_rad + math.pi) % (2 * math.pi) - math.pi
    semicircle = round(wrapped * (2**31 / math.pi))
    semicircle = (semicircle + 2**31) % 2**32 - 2**31
    return build_measurement_frame(timestamp_ms, distance, semicircle)


# Wire order, CLI label, GUI label. All fields are unsigned 32-bit counters.
STATUS_FIELDS = (
    ("crc_errors", "CRC errors", "CRC 오류"),
    ("length_errors", "Length errors", "길이 오류"),
    ("frame_timeouts", "Frame timeouts", "부분 프레임 timeout"),
    ("queue_drops", "Queue drops", "RX 큐 드롭"),
    ("ring_overflow_events", "Ring overflows", "링 overflow 횟수"),
    ("rx_dropped_bytes", "ISR dropped bytes", "ISR 폐기 바이트"),
    ("recovery_discarded_bytes", "Recovery discarded bytes", "복구 중 버퍼 폐기 바이트"),
)
STATUS_PAYLOAD = struct.Struct(">" + "I" * len(STATUS_FIELDS))
# Compatibility is explicitly limited to the original four-counter layout.
LEGACY_STATUS_PAYLOAD = struct.Struct(">4I")


def decode_status_payload(payload):
    """Decode current or legacy counters; absent legacy fields remain absent, not zero."""
    layouts = (LEGACY_STATUS_PAYLOAD, STATUS_PAYLOAD)
    layout = next((item for item in layouts if item.size == len(payload)), None)
    if layout is None:
        raise ValueError(
            f"STATUS_REPORT payload must be {LEGACY_STATUS_PAYLOAD.size} or {STATUS_PAYLOAD.size} bytes")
    return {field[0]: value for field, value in zip(STATUS_FIELDS, layout.unpack(payload))}


class TrackState(IntEnum):
    EMPTY = 0
    TENTATIVE = 1
    CONFIRMED = 2
    COASTING = 3
    LOST = 4


class TrackReportResult(IntEnum):
    STARTED = 0
    UPDATED = 1
    GATE_REJECTED = 2
    TIME_REJECTED = 3
    RESET = 4


class TrackReportFlag(IntFlag):
    INPUT_PRESENT = 1 << 0
    ESTIMATE_VALID = 1 << 1


TRACK_PAYLOAD = struct.Struct('>IIIffffBBBB')


@dataclass(frozen=True)
class TrackReport:
    """Sensor timestamps and board estimates; no PC wall-clock timestamps."""

    report_sequence: int
    input_timestamp_ms: int
    estimate_timestamp_ms: int
    range_m: float
    azimuth_rad: float
    range_rate_mps: float
    azimuth_rate_radps: float
    state: TrackState
    result: TrackReportResult
    flags: TrackReportFlag

    @property
    def input_present(self) -> bool:
        return bool(self.flags & TrackReportFlag.INPUT_PRESENT)

    @property
    def estimate_valid(self) -> bool:
        return bool(self.flags & TrackReportFlag.ESTIMATE_VALID)


def decode_track_payload(payload: bytes) -> TrackReport:
    """Validate the current TRACK layout after the stream parser checks CRC.

    Unknown enum values/reserved bits require an explicit protocol update.
    Numeric zero is a valid estimate or sensor timestamp when its flag is set.
    """
    if len(payload) != TRACK_PAYLOAD.size:
        raise ValueError('TRACK_REPORT payload must be 32 bytes')
    sequence, input_ms, estimate_ms, distance, angle, speed, rate, state, result, flags, reserved = (
        TRACK_PAYLOAD.unpack(payload))
    state = TrackState(state)
    result = TrackReportResult(result)
    if reserved != 0 or flags & ~int(TrackReportFlag.INPUT_PRESENT | TrackReportFlag.ESTIMATE_VALID):
        raise ValueError('TRACK_REPORT reserved byte/bits must be zero')
    estimates = (distance, angle, speed, rate)
    if not all(math.isfinite(value) for value in estimates):
        raise ValueError('TRACK_REPORT estimates must be finite')
    report = TrackReport(sequence, input_ms, estimate_ms, *estimates,
                         state, result, TrackReportFlag(flags))
    active = state in (TrackState.TENTATIVE, TrackState.CONFIRMED, TrackState.COASTING)
    if report.estimate_valid != active:
        raise ValueError('TRACK_REPORT state and estimate validity disagree')
    if not report.estimate_valid and (estimate_ms != 0 or any(estimates)):
        raise ValueError('TRACK_REPORT invalid estimates must be zero')
    if not report.input_present and input_ms != 0:
        raise ValueError('TRACK_REPORT absent input timestamp must be zero')
    return report
