#include "protocol/protocol_frame.h"

#include <errno.h>
#include <stddef.h>
#include <string.h>

#include "common/crc16.h"

bool protocol_frame_is_valid(const struct protocol_frame *p_frame)
{
    return p_frame != NULL && p_frame->payload_length > 0 && p_frame->payload_length <= PROTOCOL_MAX_PAYLOAD;
}

uint16_t protocol_frame_crc(const struct protocol_frame *p_frame)
{
    uint16_t crc = CRC16_INITIAL_VALUE;

    crc = crc16_update(crc, p_frame->payload_length);
    crc = crc16_update(crc, p_frame->type);

    for (uint8_t i = 0; i < p_frame->payload_length; ++i) {
        crc = crc16_update(crc, p_frame->payload[i]);
    }

    return crc;
}

int protocol_frame_encode(const struct protocol_frame *p_frame, uint8_t *out_buffer, size_t capacity)
{
    if (!protocol_frame_is_valid(p_frame) || out_buffer == NULL) {
        return -EINVAL;
    }

    const size_t length = p_frame->payload_length;
    const size_t frame_size = length + PROTOCOL_FRAME_OVERHEAD;

    if (capacity < frame_size) {
        return -ENOSPC;
    }

    out_buffer[0] = PROTOCOL_STX;
    out_buffer[1] = p_frame->payload_length;
    out_buffer[2] = (uint8_t)p_frame->type;

    memcpy(&out_buffer[3], p_frame->payload, length);

    const uint16_t crc = protocol_frame_crc(p_frame);

    out_buffer[3 + length] = (uint8_t)(crc >> 8);
    out_buffer[4 + length] = (uint8_t)crc;

    return (int)frame_size;

}
