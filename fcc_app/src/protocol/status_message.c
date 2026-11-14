#include "protocol/status_message.h"

#include <errno.h>

#include "common/byte_order.h"

_Static_assert(STATUS_REPORT_PAYLOAD_LENGTH <= PROTOCOL_MAX_PAYLOAD, "Status report exceeds payload capacity");

int status_message_encode(const struct status_report *p_stats, struct protocol_frame *out_frame)
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

    write_u32_be(p_stats->ring_overflow_events, &out_frame->payload[16]);
    write_u32_be(p_stats->rx_dropped_bytes, &out_frame->payload[20]);
    write_u32_be(p_stats->recovery_discarded_bytes, &out_frame->payload[24]);

    return 0;
}
