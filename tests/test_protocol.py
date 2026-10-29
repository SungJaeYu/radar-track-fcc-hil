"""현재 펌웨어 XOR 규격과 시나리오 측정 변환 검증."""
import math
import struct

import pytest
from pc_sim import protocol


def test_init_wire_format():
    assert protocol.build_init_frame() == bytes.fromhex("7e 01 01 00 01")


def test_radar_wire_format():
    assert protocol.build_radar_frame(100, 1000, -1073741824) == bytes.fromhex(
        "7e 0a 02 00 00 00 64 03 e8 c0 00 00 00 4d")


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
    frame = protocol.build_radar_frame_from_radians(100, 1000.4, angle)
    assert frame[:3] == bytes.fromhex("7e 0a 02")
    assert struct.unpack(">IHi", frame[3:-1]) == (100, 1000, expected)


@pytest.mark.parametrize("distance", [0, 0.1, -1, 65536, math.inf, math.nan])
def test_scenario_rejects_unrepresentable_ranges(distance):
    with pytest.raises(ValueError):
        protocol.build_radar_frame_from_radians(0, distance, 0)


@pytest.mark.parametrize("angle", [math.inf, math.nan])
def test_scenario_rejects_nonfinite_angles(angle):
    with pytest.raises(ValueError):
        protocol.build_radar_frame_from_radians(0, 100, angle)
