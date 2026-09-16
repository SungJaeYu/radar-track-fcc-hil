import argparse
import glob
import serial

if __package__:
    from .protocol import encode_frame
else:
    from protocol import encode_frame


def find_uart_port():
    candidates = (
        glob.glob("/dev/cu.usbserial-*")
        + glob.glob("/dev/cu.SLAB_USBtoUART*")
        + glob.glob("/dev/cu.wchusbserial*")
    )

    if not candidates:
        raise RuntimeError("USB-UART port not found")

    if len(candidates) > 1:
        print("Multiple UART ports found:")
        for port in candidates:
            print(" ", port)
        raise RuntimeError("Specify the port explicitly")

    return candidates[0]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("payload")
    parser.add_argument("--port")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument(
        "--type", type=lambda value: int(value, 0), choices=(1, 2, 3, 4),
        default=1, help="1=INIT (default), 2=RADAR_MEAS, 3=STATUS, 4=COMMAND",
    )

    args = parser.parse_args()

    payload = args.payload.encode("utf-8")

    if len(payload) == 0 or len(payload) > 32:
        parser.error("payload length must be 1..32 UTF-8 bytes")

    frame = encode_frame(args.type, payload)
    port = args.port or find_uart_port()

    with serial.Serial(port, args.baud) as ser:
        ser.write(frame)
        ser.flush()

    print(f"port: {port}")
    print("sent:", frame.hex(" "))


if __name__ == "__main__":
    main()
