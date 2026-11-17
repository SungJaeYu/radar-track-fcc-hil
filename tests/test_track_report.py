"""PC decoding of the firmware's fixed, big-endian TRACK_REPORT contract."""
import math
import struct

import pytest

from pc_sim import protocol


# Sequence 0xffffffff, input 300 ms, estimate 200 ms, 1000 m, -0.5 rad,
# -20 m/s, 0.25 rad/s, CONFIRMED / TIME_REJECTED, both flags set.
PAYLOAD = bytes.fromhex(
    'ffffffff 0000012c 000000c8 447a0000 bf000000 c1a00000 3e800000 02 03 03 00')


def test_track_decode_fixed_wire():
    report = protocol.decode_track_payload(PAYLOAD)
    assert report.report_sequence == 0xffffffff
    assert report.input_timestamp_ms == 300
    assert report.estimate_timestamp_ms == 200
    assert (report.range_m, report.azimuth_rad) == (1000, -0.5)
    assert (report.range_rate_mps, report.azimuth_rate_radps) == (-20, 0.25)
    assert report.state is protocol.TrackState.CONFIRMED
    assert report.result is protocol.TrackReportResult.TIME_REJECTED
    assert report.input_present and report.estimate_valid


@pytest.mark.parametrize('size', [0, 16, 28, 31, 33])
def test_track_rejects_wrong_length(size):
    with pytest.raises(ValueError, match='32 bytes'):
        protocol.decode_track_payload(bytes(size))


@pytest.mark.parametrize('offset,value', [(28, 5), (29, 5), (30, 7), (31, 1)])
def test_track_rejects_unknown_codes_and_reserved_bits(offset, value):
    payload = bytearray(PAYLOAD)
    payload[offset] = value
    with pytest.raises(ValueError):
        protocol.decode_track_payload(payload)


@pytest.mark.parametrize('offset', [12, 16, 20, 24])
@pytest.mark.parametrize('value', [math.nan, math.inf, -math.inf])
def test_track_rejects_nonfinite_fields(offset, value):
    payload = bytearray(PAYLOAD)
    struct.pack_into('>f', payload, offset, value)
    with pytest.raises(ValueError, match='finite'):
        protocol.decode_track_payload(payload)


@pytest.mark.parametrize('state', [0, 4])
def test_track_decodes_invalid_estimate_without_plotting_zero(state):
    payload = struct.pack('>IIIffffBBBB', 8, 300, 0, 0, 0, 0, 0, state, 4, 1, 0)
    report = protocol.decode_track_payload(payload)
    assert report.input_present and not report.estimate_valid
    assert report.range_m == 0


@pytest.mark.parametrize('state,flags', [(0, 3), (4, 3), (1, 1), (2, 1), (3, 1)])
def test_track_rejects_state_validity_mismatch(state, flags):
    payload = struct.pack('>IIIffffBBBB', 8, 300, 0, 0, 0, 0, 0, state, 4, flags, 0)
    with pytest.raises(ValueError, match='state'):
        protocol.decode_track_payload(payload)


@pytest.mark.parametrize('field', [2, 3, 4, 5, 6])
def test_track_rejects_nonzero_invalid_estimate(field):
    values = [8, 300, 0, 0, 0, 0, 0, 0, 4, 1, 0]
    values[field] = 1
    with pytest.raises(ValueError, match='zero'):
        protocol.decode_track_payload(struct.pack('>IIIffffBBBB', *values))


def test_track_flags_distinguish_absent_input_from_timestamp_zero():
    report = protocol.decode_track_payload(bytes.fromhex(
        '00000000 00000000 00000000 00000000 80000000 00000000 00000000 01 00 03 00'))
    assert report.input_present and report.estimate_valid
    assert report.input_timestamp_ms == report.estimate_timestamp_ms == 0
    assert math.copysign(1, report.azimuth_rad) == -1
    # No-input reports use zero as padding, not as a timestamp sentinel.
    report = protocol.decode_track_payload(bytes(29) + b'\x04\x00\x00')
    assert not report.input_present and not report.estimate_valid
    payload = bytearray(bytes(29) + b'\x04\x00\x00')
    payload[7] = 1
    with pytest.raises(ValueError, match='input'):
        protocol.decode_track_payload(payload)
