"""Validate recorded log-stress traffic without serial ports or flashing a board."""

import importlib.util
from pathlib import Path
import struct

import pytest

from pc_sim.protocol import encode_frame

SCRIPT = Path(__file__).resolve().parents[1] / 'fcc_app/tests/log_stress/board_check.py'
spec = importlib.util.spec_from_file_location('log_stress_board_check', SCRIPT)
board_check = importlib.util.module_from_spec(spec)
spec.loader.exec_module(board_check)

STATUS = encode_frame(6, bytes(28))
TRACK = encode_frame(7, struct.pack('>IIIffffBBBB',
                                  0, 100, 100, 1000.0, 0.0, 0.0, 0.0, 1, 0, 3, 0))


@pytest.mark.parametrize('bytewise', [False, True])
def test_log_stress_accepts_mixed_reports(bytewise):
    # Reports can share a read or span reads; only STATUS counts as a query reply.
    wire = TRACK + STATUS + TRACK + STATUS
    chunks = [bytes([byte]) for byte in wire] if bytewise else [wire]
    records = [{'tx': encode_frame(3, b'\x00').hex()}]
    records.extend({'rx': chunk.hex()} for chunk in chunks)
    assert board_check.check_wire(records, 2)


def test_log_stress_accepts_status_only_capture():
    # Previous STATUS-only bench evidence can still be checked.
    assert board_check.check_wire([{'rx': (STATUS * 2).hex()}], 2)


@pytest.mark.parametrize('wire,expected_responses', [
    pytest.param(TRACK, 0, id='no-status'),
    pytest.param(STATUS, 2, id='wrong-status-count'),
    pytest.param(encode_frame(6, struct.pack('>7I', 1, 0, 0, 0, 0, 0, 0)), 1,
                 id='nonzero-counter'),
    pytest.param(encode_frame(6, bytes(16)), 1, id='legacy-status'),
    pytest.param(encode_frame(7, bytes(31)) + STATUS, 1, id='wrong-track-length'),
    pytest.param(encode_frame(8, b'\x00') + STATUS, 1, id='unknown-type'),
    pytest.param(TRACK[:-1] + bytes([TRACK[-1] ^ 1]) + STATUS, 1, id='bad-crc'),
    pytest.param(b'\x7e\x21' + STATUS, 1, id='bad-length'),
    pytest.param(STATUS + TRACK[:-1], 1, id='partial-tail'),
])
def test_log_stress_rejects_invalid_capture(wire, expected_responses):
    assert not board_check.check_wire([{'rx': wire.hex()}], expected_responses)
