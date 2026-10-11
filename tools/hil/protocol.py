"""Current learning firmware protocol: XOR checksum, big-endian radar payload.

Separate from the CRC16 protocol in pc_sim/protocol.py.
"""
import struct

STX = 0x7E
TYPE_RADAR_MEAS = 0x02


def calculate_checksum(frame_type, payload):
    checksum = frame_type

    for byte in payload:
        checksum ^= byte

    return checksum


def encode_frame(frame_type, payload):
    """STX | payload length | type | payload | XOR(type, payload)."""
    return (bytes([STX, len(payload), frame_type]) + payload
            + bytes([calculate_checksum(frame_type, payload)]))


def build_radar_frame(timestamp_ms, range_m, azimuth_sc):
    """Encode int32 semicircle azimuth (radians = azimuth_sc * pi / 2**31)."""
    payload = struct.pack(
        ">IHi",
        timestamp_ms,
        range_m,
        azimuth_sc
    )

    return encode_frame(TYPE_RADAR_MEAS, payload)
