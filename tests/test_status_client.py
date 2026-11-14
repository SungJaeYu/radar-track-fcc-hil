"""STATUS transactions with deterministic time and an in-memory serial port."""

import pytest

from pc_sim import status_client
from pc_sim.protocol import encode_frame

REPORT = bytes.fromhex(
    "7e 10 06 12 34 56 78 80 00 00 00 00 00 00 01 ff ff ff ff eb 11"
)
EXPECTED = {
    "crc_errors": 0x12345678,
    "length_errors": 0x80000000,
    "frame_timeouts": 1,
    "queue_drops": 0xffffffff,
}


class FakePort:
    def __init__(self, chunks=()):
        self.chunks = list(chunks)
        self.timeout = 9.0
        self.write_timeout = 8.0
        self.now = 0.0
        self.writes = []
        self.read_timeouts = []
        self.cleared = False
        self.closed = False
        self.short_write = False
        self.read_error = None
        self.write_error = None

    def reset_input_buffer(self):
        self.cleared = True

    def write(self, data):
        assert self.cleared
        if self.write_error:
            raise self.write_error
        self.writes.append(data)
        return len(data) - int(self.short_write)

    def read(self, size):
        self.read_timeouts.append(self.timeout)
        if self.read_error:
            raise self.read_error
        if self.chunks:
            self.now += min(0.001, self.timeout)
            data = self.chunks.pop(0)
            if len(data) > size:
                self.chunks.insert(0, data[size:])
            return data[:size]
        self.now += self.timeout
        return b""

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.closed = True


def use_clock(monkeypatch, port):
    monkeypatch.setattr(status_client.time, "monotonic", lambda: port.now)


@pytest.mark.parametrize("chunks", [[REPORT], [bytes([b]) for b in REPORT]])
def test_status_response(monkeypatch, chunks):
    port = FakePort(chunks)
    use_clock(monkeypatch, port)

    assert status_client.request_status(port) == EXPECTED
    assert port.writes == [encode_frame(3, b"\x00")]
    assert (port.timeout, port.write_timeout) == (9.0, 8.0)


def test_skips_errors_and_other_types(monkeypatch):
    corrupt = bytearray(REPORT)
    corrupt[-1] ^= 1
    stream = (b"\x00\x7e\x00" + bytes(corrupt) + encode_frame(1, b"\x00")
              + encode_frame(6, b"\x00") + REPORT)
    port = FakePort([stream])
    use_clock(monkeypatch, port)

    assert status_client.request_status(port) == EXPECTED
    assert len(port.writes) == 1


@pytest.mark.parametrize("chunks", [[], [REPORT[:10]], [b"\x00"] * 500])
def test_total_deadline_is_not_extended(monkeypatch, chunks):
    port = FakePort(chunks)
    use_clock(monkeypatch, port)

    with pytest.raises(TimeoutError):
        status_client.request_status(port, timeout=0.12)
    assert port.now == pytest.approx(0.12)
    assert all(0 < value <= 0.05 for value in port.read_timeouts)
    assert (port.timeout, port.write_timeout) == (9.0, 8.0)
    assert len(port.writes) == 1


def test_late_response_is_rejected(monkeypatch):
    port = FakePort([REPORT])
    use_clock(monkeypatch, port)

    with pytest.raises(TimeoutError):
        status_client.request_status(port, timeout=0.001)


@pytest.mark.parametrize("failure", ["short_write", "write_error", "read_error"])
def test_io_failures_restore_timeouts(monkeypatch, failure):
    port = FakePort()
    use_clock(monkeypatch, port)
    setattr(port, failure, True if failure == "short_write" else OSError("disconnected"))

    with pytest.raises(OSError):
        status_client.request_status(port)
    assert (port.timeout, port.write_timeout) == (9.0, 8.0)


@pytest.mark.parametrize("value", [0, -1, float("nan"), float("inf")])
def test_invalid_timeout_does_not_touch_port(value):
    port = FakePort()
    with pytest.raises(ValueError):
        status_client.request_status(port, value)
    assert not port.cleared
    assert port.writes == []


@pytest.mark.parametrize("success", [True, False])
def test_cli_output_and_port_cleanup(monkeypatch, capsys, success):
    port = FakePort([REPORT] if success else [])
    use_clock(monkeypatch, port)
    monkeypatch.setattr(status_client.serial, "Serial", lambda *a, **kw: port)

    result = status_client.main(["--port", "fake", "--timeout", "0.12"])

    output = capsys.readouterr()
    assert port.closed
    if success:
        assert result == 0
        assert "CRC errors:     305419896" in output.out
        assert "Queue drops:    4294967295" in output.out
        assert output.err == ""
    else:
        assert result == 1
        assert "Timed out" in output.err
        assert output.out == ""


def test_cli_open_failure(monkeypatch, capsys):
    def fail_open(*args, **kwargs):
        raise status_client.serial.SerialException("port busy")
    monkeypatch.setattr(status_client.serial, "Serial", fail_open)

    assert status_client.main(["--port", "fake"]) == 1
    assert "port busy" in capsys.readouterr().err


@pytest.mark.parametrize("option,value", [("--timeout", "nan"), ("--timeout", "0"),
                                           ("--timeout", "bad"), ("--baud", "0")])
def test_cli_invalid_arguments(monkeypatch, option, value):
    def unexpected_open(*args, **kwargs):
        pytest.fail("Invalid settings must not open the port")
    monkeypatch.setattr(status_client.serial, "Serial", unexpected_open)

    with pytest.raises(SystemExit) as error:
        status_client.main(["--port", "fake", option, value])
    assert error.value.code == 2


@pytest.mark.parametrize("extended", [False, True])
def test_cli_shared_status_fields(monkeypatch, capsys, extended):
    payload = bytes.fromhex("12345678 80000000 00000001 ffffffff")
    if extended:
        payload += bytes.fromhex("00000007 01020304 ffffffff")
    port = FakePort([encode_frame(6, payload)])
    use_clock(monkeypatch, port)
    monkeypatch.setattr(status_client.serial, "Serial", lambda *args, **kwargs: port)
    assert status_client.main(["--port", "fake"]) == 0
    output = capsys.readouterr().out
    expected_overflows = "7" if extended else "unsupported"
    expected_discarded = "4294967295" if extended else "unsupported"
    assert f"Ring overflows: {expected_overflows}" in output
    assert f"Recovery discarded bytes: {expected_discarded}" in output
