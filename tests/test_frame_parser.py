"""PC frame assembly, error recovery and reset contract tests."""

import pytest

from pc_sim.frame_parser import Frame, FrameParser, ParserState
from pc_sim.protocol import FRAME_TYPE_INIT, FRAME_TYPE_STATUS_REPORT, encode_frame


# Fixed reference bytes keep basic acceptance independent of the Python encoder.
INIT_WIRE = bytes.fromhex("7e 01 01 00 c8 9d")
EXPECTED_INIT = Frame(frame_type=FRAME_TYPE_INIT, payload=b"\x00")


def test_complete_frame():
    parser = FrameParser()

    frames = parser.feed(INIT_WIRE)

    assert frames == [EXPECTED_INIT]
    assert isinstance(frames[0].payload, bytes)
    assert parser.state == ParserState.WAIT_STX
    assert parser.crc_errors == 0
    assert parser.length_errors == 0
    assert parser.feed(b"") == []


def test_byte_by_byte():
    parser = FrameParser()

    for byte in INIT_WIRE[:-1]:
        assert parser.feed(bytes([byte])) == []
    assert parser.state == ParserState.WAIT_CRC_L

    assert parser.feed(INIT_WIRE[-1:]) == [EXPECTED_INIT]
    assert parser.state == ParserState.WAIT_STX


@pytest.mark.parametrize("cut", range(1, len(INIT_WIRE)))
def test_split_frame(cut):
    parser = FrameParser()

    assert parser.feed(INIT_WIRE[:cut]) == []
    assert parser.state != ParserState.WAIT_STX
    # Empty reads must not discard partial input.
    assert parser.feed(b"") == []
    assert parser.feed(INIT_WIRE[cut:]) == [EXPECTED_INIT]
    assert parser.state == ParserState.WAIT_STX


def test_consecutive_frames():
    parser = FrameParser()
    # Fixed STATUS vector: counters 0x12345678, 0x80000000, 1, UINT32_MAX.
    status_wire = bytes.fromhex(
        "7e 10 06 12 34 56 78 80 00 00 00 00 00 00 01 ff ff ff ff eb 11"
    )
    expected_status = Frame(FRAME_TYPE_STATUS_REPORT, status_wire[3:-2])

    frames = parser.feed(INIT_WIRE + status_wire)

    assert frames == [EXPECTED_INIT, expected_status]
    assert parser.state == ParserState.WAIT_STX
    # Subsequent assembly must not change previously returned payloads.
    assert parser.feed(INIT_WIRE) == [EXPECTED_INIT]
    assert frames == [EXPECTED_INIT, expected_status]


def test_noise_before_frame():
    parser = FrameParser()

    frames = parser.feed(b"\x00\xff\x13" + INIT_WIRE)

    assert frames == [EXPECTED_INIT]
    assert parser.crc_errors == 0
    assert parser.length_errors == 0


def test_stx_inside_payload():
    parser = FrameParser()
    payload = b"\x7e\x00\x7e"
    wire = encode_frame(FRAME_TYPE_INIT, payload)

    frames = parser.feed(wire)

    assert frames == [Frame(FRAME_TYPE_INIT, payload)]
    assert parser.state == ParserState.WAIT_STX
    assert parser.crc_errors == 0
    assert parser.length_errors == 0


def test_bad_crc_then_valid_frame():
    parser = FrameParser()
    corrupt = bytearray(INIT_WIRE)
    corrupt[-1] ^= 1

    assert parser.feed(bytes(corrupt)) == []
    assert parser.crc_errors == 1
    assert parser.length_errors == 0
    assert parser.state == ParserState.WAIT_STX

    assert parser.feed(INIT_WIRE) == [EXPECTED_INIT]
    assert parser.crc_errors == 1


@pytest.mark.parametrize("length", [0, 33, 255])
def test_bad_length_then_valid_frame(length):
    parser = FrameParser()

    assert parser.feed(bytes([0x7e, length])) == []
    assert parser.length_errors == 1
    assert parser.crc_errors == 0
    assert parser.state == ParserState.WAIT_STX

    assert parser.feed(INIT_WIRE) == [EXPECTED_INIT]
    assert parser.length_errors == 1


def test_maximum_payload():
    parser = FrameParser()
    payload = bytes(range(32))
    wire = encode_frame(FRAME_TYPE_INIT, payload)

    frames = parser.feed(wire)

    assert frames == [Frame(FRAME_TYPE_INIT, payload)]
    assert parser.state == ParserState.WAIT_STX
    assert parser.crc_errors == 0
    assert parser.length_errors == 0


def test_reset_discards_partial_frame_and_preserves_counters():
    parser = FrameParser()
    corrupt = bytearray(INIT_WIRE)
    corrupt[-1] ^= 1
    assert parser.feed(bytes(corrupt) + b"\x7e\x00") == []
    assert (parser.crc_errors, parser.length_errors) == (1, 1)
    assert parser.feed(INIT_WIRE[:-1]) == []
    assert parser.state == ParserState.WAIT_CRC_L
    assert parser.payload == bytearray(b"\x00")

    parser.reset()

    assert parser.state == ParserState.WAIT_STX
    assert parser.payload_length == 0
    assert parser.frame_type == 0
    assert parser.payload == bytearray()
    assert parser.received_crc == 0
    assert (parser.crc_errors, parser.length_errors) == (1, 1)
    # The old CRC tail alone cannot complete a frame after reset.
    assert parser.feed(INIT_WIRE[-1:]) == []
    assert parser.feed(INIT_WIRE) == [EXPECTED_INIT]
    assert (parser.crc_errors, parser.length_errors) == (1, 1)
