#ifndef PROTOCOL_FRAME_H
#define PROTOCOL_FRAME_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define STX 0x7E

#define MAX_PAYLOAD 32

typedef enum {
    TYPE_INIT       = 0x01,
    TYPE_RADAR_MEAS = 0x02,
    TYPE_STATUS     = 0x03,
    TYPE_COMMAND    = 0x04,
    TYPE_RESET      = 0x05,
} FrameType;

struct protocol_frame {
    FrameType type;
    uint8_t payload_length;
    uint8_t payload[MAX_PAYLOAD];
};

bool protocol_frame_is_valid(const struct protocol_frame *p_frame);

uint16_t protocol_frame_crc(const struct protocol_frame *p_frame);

int protocol_frame_encode(const struct protocol_frame *p_frame, uint8_t *p_buffer, size_t capacity);

#endif