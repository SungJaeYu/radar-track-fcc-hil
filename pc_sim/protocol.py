"""현재 STM32 규격: XOR 체크섬, big-endian 10바이트 레이더 payload."""
import math
import struct

STX = 0x7E
TYPE_INIT = 0x01
TYPE_RADAR_MEAS = 0x02


def calculate_checksum(frame_type, payload):
    checksum = frame_type

    for byte in payload:
        checksum ^= byte

    return checksum


def encode_frame(frame_type, payload):
    """STX | payload length | type | payload | XOR(type, payload)."""
    if not 1 <= len(payload) <= 32:
        raise ValueError("payload length must be 1..32 bytes")
    return (bytes([STX, len(payload), frame_type]) + payload
            + bytes([calculate_checksum(frame_type, payload)]))


def build_init_frame():
    """새 측정 시퀀스 시작. 현재 파서는 빈 payload를 거부하므로 1바이트를 넣는다."""
    return encode_frame(TYPE_INIT, b"\x00")


def build_radar_frame(timestamp_ms, range_m, azimuth_sc):
    """Encode int32 semicircle azimuth (radians = azimuth_sc * pi / 2**31)."""
    payload = struct.pack(
        ">IHi",
        timestamp_ms,
        range_m,
        azimuth_sc
    )

    return encode_frame(TYPE_RADAR_MEAS, payload)


def build_radar_frame_from_radians(timestamp_ms, range_m, azimuth_rad):
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
    return build_radar_frame(timestamp_ms, distance, semicircle)
