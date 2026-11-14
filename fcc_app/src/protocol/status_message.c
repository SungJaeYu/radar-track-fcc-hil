#include "protocol/status_message.h"

#include <errno.h>

#define STATUS_REPORT_PAYLOAD_LENGTH 16

_Static_assert(STATUS_REPORT_PAYLOAD_LENGTH <= PROTOCOL_MAX_PAYLOAD, "Status report exceeds payload capacity");

static void write_u32_be(uint32_t value, uint8_t *out_buffer)
{
    // The caller must provide at least four writable bytes.
    out_buffer[0] = (uint8_t)(value >> 24);
    out_buffer[1] = (uint8_t)(value >> 16);
    out_buffer[2] = (uint8_t)(value >> 8);
    out_buffer[3] = (uint8_t)value;
}

int status_message_encode(const struct rx_frame_stats *p_stats, struct protocol_frame *out_frame)
{
    if (p_stats == NULL || out_frame == NULL) {
        return -EINVAL;
    }

    out_frame->type = FRAME_TYPE_STATUS_REPORT;
    out_frame->payload_length = STATUS_REPORT_PAYLOAD_LENGTH;

    write_u32_be(p_stats->crc_errors,     &out_frame->payload[0]);
    write_u32_be(p_stats->length_errors,  &out_frame->payload[4]);
    write_u32_be(p_stats->frame_timeouts, &out_frame->payload[8]);
    write_u32_be(p_stats->queue_drops,    &out_frame->payload[12]);

    return 0;
}
