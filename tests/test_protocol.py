"""현재 펌웨어 CRC-16 규격과 시나리오 측정 변환 검증."""
import math
import struct

import pytest
from pc_sim import protocol


def test_init_wire_format():
    assert protocol.build_init_frame() == bytes.fromhex("7e 01 01 00 c8 9d")


def test_radar_wire_format():
    assert protocol.build_measurement_frame(100, 1000, -1073741824) == bytes.fromhex(
        "7e 0a 02 00 00 00 64 03 e8 c0 00 00 00 0f b1")


@pytest.mark.parametrize("payload", [b"", b"x" * 33])
def test_reject_payload_lengths_firmware_cannot_parse(payload):
    with pytest.raises(ValueError):
        protocol.encode_frame(1, payload)


@pytest.mark.parametrize("angle,expected", [
    (0, 0), (math.pi / 2, 1073741824),
    (-math.pi / 2, -1073741824), (math.pi, -2147483648),
    (-math.pi, -2147483648), (3 * math.pi, -2147483648),
])
def test_scenario_radians_to_wire(angle, expected):
    frame = protocol.build_measurement_frame_from_radians(100, 1000.4, angle)
    assert frame[:3] == bytes.fromhex("7e 0a 02")
    assert struct.unpack(">IHi", frame[3:-2]) == (100, 1000, expected)


@pytest.mark.parametrize("distance", [0, 0.1, -1, 65536, math.inf, math.nan])
def test_scenario_rejects_unrepresentable_ranges(distance):
    with pytest.raises(ValueError):
        protocol.build_measurement_frame_from_radians(0, distance, 0)


@pytest.mark.parametrize("angle", [math.inf, math.nan])
def test_scenario_rejects_nonfinite_angles(angle):
    with pytest.raises(ValueError):
        protocol.build_measurement_frame_from_radians(0, 100, angle)


def test_decode_status_payload():
    payload = bytes.fromhex(
        "00 00 00 01"
        "00 00 00 17"
        "00 00 01 c8"
        "ff ff ff ff"
    )

    stats = protocol.decode_status_payload(payload)

    assert stats == {
        "crc_errors": 1,
        "length_errors": 23,
        "frame_timeouts": 456,
        "queue_drops": 4294967295,
    }


def test_decode_status_payload_zero():
    stats = protocol.decode_status_payload(bytes(16))

    assert stats == {
        "crc_errors": 0,
        "length_errors": 0,
        "frame_timeouts": 0,
        "queue_drops": 0,
    }


@pytest.mark.parametrize("length", [0, 15, 17])
def test_decode_status_payload_invalid_length(length):
    with pytest.raises(ValueError, match="STATUS_REPORT payload must be 16, 28 or 32 bytes"):
        protocol.decode_status_payload(bytes(length))


def test_decode_extended_status_golden_payload():
    # Literal wire bytes are independent of the production field definition.
    payload = bytes.fromhex(
        "12345678 80000000 00000001 ffffffff 00000007 01020304 ffffffff")
    assert protocol.decode_status_payload(payload) == {
        "crc_errors": 0x12345678, "length_errors": 0x80000000,
        "frame_timeouts": 1, "queue_drops": 0xffffffff,
        "ring_overflow_events": 7, "rx_dropped_bytes": 0x01020304,
        "recovery_discarded_bytes": 0xffffffff,
    }


@pytest.mark.parametrize("index", range(7))
@pytest.mark.parametrize("value", [0, 0xffffffff])
def test_extended_status_field_boundaries(index, value):
    values = [0] * 7
    values[index] = value
    stats = protocol.decode_status_payload(struct.pack(">7I", *values))
    assert list(stats.values()) == values


@pytest.mark.parametrize("length", [20, 24, 27, 29, 31, 33])
def test_reject_unknown_status_layout(length):
    with pytest.raises(ValueError):
        protocol.decode_status_payload(bytes(length))


@pytest.mark.parametrize("value", [0, 1, 0xffffffff])
def test_status_stale_counter_current_and_legacy(value):
    current = protocol.decode_status_payload(struct.pack('>8I', 0, 0, 0, 0, 0, 0, 0, value))
    assert current['stale_measurements'] == value
    assert 'stale_measurements' not in protocol.decode_status_payload(bytes(28))


@pytest.mark.parametrize('timestamp', [0, 1, 0x7fffffff, 0x80000000, 0xfffffffe, 0xffffffff])
@pytest.mark.parametrize('distance', [0, 1, 2, 65534, 65535])
@pytest.mark.parametrize('angle', [-2147483648, -2147483647, -1, 0, 1, 2147483646, 2147483647])
def test_raw_measurement_integer_domains(timestamp, distance, angle):
    # Raw encoder permits zero range for negative board tests; the scenario encoder does not.
    frame = protocol.build_measurement_frame(timestamp, distance, angle)
    assert struct.unpack('>IHi', frame[3:-2]) == (timestamp, distance, angle)


@pytest.mark.parametrize('timestamp,distance,angle', [
    (-1, 1, 0), (0x100000000, 1, 0), (0, -1, 0), (0, 65536, 0),
    (0, 1, -2147483649), (0, 1, 2147483648),
])
def test_raw_measurement_rejects_unrepresentable_integers(timestamp, distance, angle):
    with pytest.raises(struct.error):
        protocol.build_measurement_frame(timestamp, distance, angle)


@pytest.mark.parametrize('distance,expected', [
    (0.5, None), (math.nextafter(.5, math.inf), 1), (1, 1),
    (65535, 65535), (math.nextafter(65535.5, 0), 65535), (65535.5, None),
])
def test_range_rounding_boundaries(distance, expected):
    if expected is None:
        with pytest.raises(ValueError):
            protocol.build_measurement_frame_from_radians(0, distance, 0)
    else:
        frame = protocol.build_measurement_frame_from_radians(0, distance, 0)
        assert struct.unpack('>IHi', frame[3:-2])[1] == expected


@pytest.mark.parametrize('length', range(34))
def test_frame_payload_length_domain(length):
    payload = bytes(range(length))
    if 1 <= length <= 32:
        frame = protocol.encode_frame(2, payload)
        assert frame[1] == length and frame[3:-2] == payload and len(frame) == length + 5
    else:
        with pytest.raises(ValueError):
            protocol.encode_frame(2, payload)


@pytest.mark.parametrize('kind', [-1, 0, 1, 7, 254, 255, 256])
def test_frame_type_byte_boundary(kind):
    if 0 <= kind <= 255:
        assert protocol.encode_frame(kind, b'x')[2] == kind
    else:
        with pytest.raises(ValueError):
            protocol.encode_frame(kind, b'x')
