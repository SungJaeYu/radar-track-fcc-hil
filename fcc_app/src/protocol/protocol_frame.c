#include <stddef.h>
#include <errno.h>
#include <string.h>

#include "protocol/protocol_frame.h"
#include "common/crc16.h"

bool protocol_frame_is_valid(const struct protocol_frame *p_frame)
{
    return p_frame != NULL && p_frame->payload_length > 0 && p_frame->payload_length <= MAX_PAYLOAD;
}

uint16_t protocol_frame_crc(const struct protocol_frame *p_frame)
{
    uint16_t crc = INIT_CRC;

    crc = crc16_update(crc, p_frame->payload_length);
    crc = crc16_update(crc, p_frame->type);

    for (uint8_t i = 0; i < p_frame->payload_length; ++i) {
        crc = crc16_update(crc, p_frame->payload[i]);
    }
    
    return crc;
}

int protocol_frame_encode(const struct protocol_frame *p_frame, uint8_t *p_buffer, size_t capacity)
{
    if (!protocol_frame_is_valid(p_frame) || p_buffer == NULL) {
        return -EINVAL;
    }

    size_t length = p_frame->payload_length;
    size_t frame_size = length + 5U;

    if (capacity < frame_size) {
        return -ENOSPC;
    }

    p_buffer[0] = STX;
    p_buffer[1] = p_frame->payload_length;
    p_buffer[2] = (uint8_t)p_frame->type;

    memcpy(&p_buffer[3], p_frame->payload, length);

    uint16_t crc = protocol_frame_crc(p_frame);

    p_buffer[3 + length] = (uint8_t)(crc >> 8);
    p_buffer[4 + length] = (uint8_t)crc;

    return (int)frame_size;

}