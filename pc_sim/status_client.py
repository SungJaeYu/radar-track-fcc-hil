"""Request one diagnostic report over an exclusively owned UART connection."""

import argparse
import math
import sys
import time

import serial

from .frame_parser import FrameParser
from .protocol import (
    FRAME_TYPE_STATUS_REPORT,
    FRAME_TYPE_STATUS_REQUEST,
    decode_status_payload,
    encode_frame,
)


def request_status(port, timeout: float = 2.0) -> dict[str, int]:
    """Send once and wait for a valid report within a monotonic time budget.

    Discard previously buffered input before sending. The caller owns the port
    exclusively and closes it; original read/write timeouts are restored here.
    No request ID exists, so delayed replies cannot be correlated reliably.
    Serial I/O failures propagate; missing valid responses raise TimeoutError.
    """
    if not math.isfinite(timeout) or timeout <= 0:
        raise ValueError("timeout must be finite and greater than zero")

    old_timeout = port.timeout
    old_write_timeout = port.write_timeout
    parser = FrameParser()
    try:
        port.reset_input_buffer()
        deadline = time.monotonic() + timeout
        port.write_timeout = timeout
        request = encode_frame(FRAME_TYPE_STATUS_REQUEST, b"\x00")
        if port.write(request) != len(request):
            raise OSError("Incomplete STATUS request write")

        # Do not flush: an unbounded output-drain wait would bypass the deadline.
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError("Timed out waiting for a valid STATUS_REPORT")
            port.timeout = min(0.05, remaining)
            data = port.read(64)
            if time.monotonic() >= deadline:
                raise TimeoutError("Timed out waiting for a valid STATUS_REPORT")
            for frame in parser.feed(data):
                if frame.frame_type != FRAME_TYPE_STATUS_REPORT:
                    continue
                try:
                    return decode_status_payload(frame.payload)
                except ValueError:
                    # A valid CRC does not guarantee a valid TYPE-specific payload.
                    continue
    finally:
        port.timeout = old_timeout
        port.write_timeout = old_write_timeout


def positive_timeout(value: str) -> float:
    try:
        timeout = float(value)
    except ValueError as error:
        raise argparse.ArgumentTypeError("timeout must be a number") from error
    if not math.isfinite(timeout) or timeout <= 0:
        raise argparse.ArgumentTypeError("timeout must be finite and greater than zero")
    return timeout


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description="Request and display FCC STATUS counters")
    parser.add_argument("--port", required=True)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=positive_timeout, default=2.0)
    args = parser.parse_args(argv)
    if args.baud <= 0:
        parser.error("baud must be greater than zero")

    try:
        with serial.Serial(args.port, args.baud, timeout=0.05,
                           write_timeout=args.timeout) as port:
            stats = request_status(port, args.timeout)
    except (OSError, serial.SerialException) as error:
        print(f"STATUS request failed: {error}", file=sys.stderr)
        return 1

    print(f"CRC errors:     {stats['crc_errors']}")
    print(f"Length errors:  {stats['length_errors']}")
    print(f"Frame timeouts: {stats['frame_timeouts']}")
    print(f"Queue drops:    {stats['queue_drops']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
