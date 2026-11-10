#ifndef PROTOCOL_FRAME_H
#define PROTOCOL_FRAME_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Wire layout: STX | LEN | TYPE | payload | CRC_H | CRC_L.
// LEN counts payload bytes only (1..32); total wire size is LEN + 5 bytes.
#define STX 0x7E

#define MAX_PAYLOAD 32

// Existing values remain wire-compatible. STATUS_REPORT is reserved for a future response.
typedef enum {
    FRAME_TYPE_INIT           = 0x01,
    FRAME_TYPE_MEAS_REPORT    = 0x02,
    FRAME_TYPE_STATUS_REQUEST = 0x03,
    FRAME_TYPE_COMMAND        = 0x04,
    FRAME_TYPE_RESET          = 0x05,
    // Payload format and response transmission are not implemented yet.
    FRAME_TYPE_STATUS_REPORT  = 0x06,
} FrameType;

// In-memory frame representation, not a packed wire structure.
// Serialize fields explicitly; only payload_length bytes of payload are meaningful.
struct protocol_frame {
    FrameType type;
    uint8_t payload_length;
    uint8_t payload[MAX_PAYLOAD];
};

// Return true for a non-NULL frame with a payload length in 1..MAX_PAYLOAD.
// Does not validate TYPE support, payload semantics, or a received CRC.
bool protocol_frame_is_valid(const struct protocol_frame *p_frame);

// Require a non-NULL frame with a valid payload length and a one-byte TYPE.
// Compute CRC over LEN, TYPE, and payload, excluding STX and the CRC bytes.
// No input validation is performed here.
uint16_t protocol_frame_crc(const struct protocol_frame *p_frame);

// Encode into p_buffer; capacity is the writable buffer size in bytes.
// Require a TYPE representable in one byte and non-overlapping input/output storage.
// Return the encoded byte count on success; CRC is written high byte first.
// Return -EINVAL for NULL pointers or invalid length, or -ENOSPC for insufficient capacity.
// On these errors, leave the output buffer unchanged.
int protocol_frame_encode(const struct protocol_frame *p_frame, uint8_t *p_buffer, size_t capacity);

#endif