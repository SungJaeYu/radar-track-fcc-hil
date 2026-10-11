import argparse
from contextlib import nullcontext
import math
import time
import random

if __package__:
    from .protocol import build_radar_frame
else:
    from protocol import build_radar_frame


def main():
    parser = argparse.ArgumentParser()

    parser.add_argument("--port", help="USB-UART port; omit for print-only mode")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--range", type=float, default=1000.0)
    parser.add_argument("--rate", type=float, default=-20.0)
    parser.add_argument("--noise", type=float, default=5.0)
    parser.add_argument("--azimuth", type=int, default=178956971,
                        help="int32 semicircle azimuth (default: 178956971, about 30 degrees)")
    parser.add_argument("--hz", type=float, default=10.0)
    parser.add_argument("--count", type=int, default=20)

    args = parser.parse_args()

    if not math.isfinite(args.hz) or args.hz <= 0:
        parser.error("--hz must be finite and positive")
    if args.count <= 0 or args.baud <= 0:
        parser.error("--count and --baud must be positive")
    if not all(math.isfinite(x) for x in (args.range, args.rate, args.noise)) or args.noise < 0:
        parser.error("range/rate/noise must be finite; noise must be nonnegative")
    if not -2147483648 <= args.azimuth <= 2147483647:
        parser.error("--azimuth must be -2147483648..2147483647 semicircles")

    connection = nullcontext(None)
    if args.port:
        import serial
        connection = serial.Serial(args.port, args.baud, write_timeout=2)

    dt = 1.0 / args.hz
    true_range = args.range

    with connection as ser:
        for i in range(args.count):
            timestamp_ms = int(i * dt * 1000)

            measured_range = (
                true_range
                + random.gauss(0.0, args.noise)
            )

            measured_range_int = int(round(measured_range))

            if not 1 <= measured_range_int <= 65535:
                parser.error("generated range must be 1..65535 metres")

            frame = build_radar_frame(
                timestamp_ms,
                measured_range_int,
                args.azimuth
            )

            if ser is not None:
                ser.write(frame)
                ser.flush()

            print(
                f"{i:03d} "
                f"t={timestamp_ms:5d} ms "
                f"true={true_range:8.2f} "
                f"meas={measured_range_int:5d} "
                f"frame={frame.hex(' ')}"
            )

            true_range += args.rate * dt
            if ser is not None and i + 1 < args.count:
                time.sleep(dt)

if __name__ == "__main__":
    main()
