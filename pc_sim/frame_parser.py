"""Incrementally assemble CRC-validated frames from PC UART receive chunks."""

from dataclasses import dataclass
from enum import Enum, auto

from .protocol import STX, crc16


@dataclass(frozen=True)
class Frame:
    """A CRC-validated frame; TYPE-specific payload validation belongs to the caller."""

    frame_type: int
    payload: bytes


class ParserState(Enum):
    WAIT_STX = auto()
    WAIT_LEN = auto()
    WAIT_TYPE = auto()
    READ_PAYLOAD = auto()
    WAIT_CRC_H = auto()
    WAIT_CRC_L = auto()


class FrameParser:
    """Keep partial input between calls; perform no serial I/O or timeout checks."""

    def __init__(self) -> None:
        # These are PC receive errors, separate from the board's STATUS counters.
        self.crc_errors = 0
        self.length_errors = 0
        self.reset()

    def reset(self) -> None:
        """Discard partial assembly while preserving cumulative error counters."""
        self.state = ParserState.WAIT_STX
        self.payload_length = 0
        self.frame_type = 0
        self.payload = bytearray()
        self.received_crc = 0

    def feed(self, data: bytes) -> list[Frame]:
        """Process a chunk and collect completed frames in receive order.

        Assembly state survives between calls. Incomplete input produces no frame;
        invalid lengths and CRCs increment PC counters and reset assembly.
        One chunk may complete zero, one, or multiple frames.
        """
        frames = []
        for byte in data:
            frame = self._feed_byte(byte)
            if frame is not None:
                frames.append(frame)
        return frames

    def _feed_byte(self, byte: int) -> Frame | None:
        """Consume one byte; return a frame only after successful CRC validation."""
        if self.state == ParserState.WAIT_STX:
            if byte == STX:
                self.state = ParserState.WAIT_LEN
            return None

        if self.state == ParserState.WAIT_LEN:
            # LEN counts payload bytes only; reject invalid lengths before buffering.
            if byte == 0 or byte > 32:
                self.length_errors += 1
                self.reset()
            else:
                self.payload_length = byte
                self.state = ParserState.WAIT_TYPE
            return None

        if self.state == ParserState.WAIT_TYPE:
            # Preserve TYPE without interpreting its application-specific meaning.
            self.frame_type = byte
            self.state = ParserState.READ_PAYLOAD
            return None

        if self.state == ParserState.READ_PAYLOAD:
            # LEN determines the payload boundary; STX inside payload is ordinary data.
            self.payload.append(byte)
            if (len(self.payload) == self.payload_length):
                self.state = ParserState.WAIT_CRC_H
            return None

        if self.state == ParserState.WAIT_CRC_H:
            # CRC is transmitted high byte first.
            self.received_crc = byte << 8
            self.state = ParserState.WAIT_CRC_L
            return None

        if self.state == ParserState.WAIT_CRC_L:
            # CRC covers LEN, TYPE and payload, excluding STX and the CRC bytes.
            self.received_crc |= byte
            body = bytes([self.payload_length, self.frame_type]) + bytes(self.payload)
            expected_crc = crc16(body)

            frame = None
            if self.received_crc == expected_crc:
                # Preserve immutable frame data before clearing assembly state.
                frame = Frame(
                    frame_type=self.frame_type,
                    payload=bytes(self.payload),
                )
            else:
                self.crc_errors += 1
            # Both outcomes return to STX search without clearing error counters.
            self.reset()
            return frame

        raise RuntimeError("Unexpected parser state")
