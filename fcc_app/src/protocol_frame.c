#include <stddef.h>

#include "protocol_frame.h"
#include "crc16.h"

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