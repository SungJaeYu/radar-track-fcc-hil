"""Current STM32 CRC-16 protocol and manual sender regression checks."""
import contextlib
import io
import sys
from unittest.mock import patch

import pytest

from pc_sim import protocol, small_byte_test


def test_init_frame_matches_firmware_wire_format():
    assert protocol.encode_frame(1, b'hello') == bytes.fromhex(
        '7e 05 01 68 65 6c 6c 6f 0e ab'
    )


def test_small_byte_sender_writes_init_frame():
    with patch.object(sys, 'argv', ['sender', 'hello', '--port', 'test']), \
            patch.object(small_byte_test.serial, 'Serial') as port, \
            contextlib.redirect_stdout(io.StringIO()):
        small_byte_test.main()
    port.return_value.__enter__.return_value.write.assert_called_once_with(
        bytes.fromhex('7e 05 01 68 65 6c 6c 6f 0e ab')
    )


@pytest.mark.parametrize('payload', ['', 'x' * 33, '가' * 11])
def test_small_byte_rejects_invalid_byte_length(payload):
    with patch.object(sys, 'argv', ['sender', payload, '--port', 'test']), \
            patch.object(small_byte_test.serial, 'Serial') as port, \
            contextlib.redirect_stderr(io.StringIO()), \
            pytest.raises(SystemExit) as error:
        small_byte_test.main()
    assert error.value.code == 2
    port.assert_not_called()


def test_small_byte_sender_can_send_firmware_reset():
    with patch.object(sys, 'argv', ['sender', 'r', '--type', '5', '--port', 'test']), \
            patch.object(small_byte_test.serial, 'Serial') as port, \
            contextlib.redirect_stdout(io.StringIO()):
        small_byte_test.main()
    port.return_value.__enter__.return_value.write.assert_called_once_with(
        bytes.fromhex('7e 01 05 72 5a 8c')
    )
