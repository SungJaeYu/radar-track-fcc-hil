#include "protocol/track_message.h"

#include <errno.h>

#include "common/byte_order.h"

// Guard the current uint32, float32, uint8 layout, not the correctness of field serialization.
// Revisit this check if field types or structure padding change.
_Static_assert(sizeof(struct track_report) == TRACK_REPORT_PAYLOAD_LENGTH,
               "Update TRACK field count or review structure padding");
_Static_assert(TRACK_REPORT_PAYLOAD_LENGTH <= PROTOCOL_MAX_PAYLOAD, "Track report exceeds payload capacity");


int track_message_encode(const struct track_report *p_report, struct protocol_frame *out_frame)
{
    if (p_report == NULL || out_frame == NULL) {
        return -EINVAL;
    }

    out_frame->type = FRAME_TYPE_TRACK_REPORT;
    out_frame->payload_length = TRACK_REPORT_PAYLOAD_LENGTH;

    write_u32_be(p_report->report_sequence, &out_frame->payload[0]);
    write_u32_be(p_report->input_sensor_ms, &out_frame->payload[4]);
    write_u32_be(p_report->estimate_sensor_ms, &out_frame->payload[8]);
    write_f32_be(p_report->range_m, &out_frame->payload[12]);
    write_f32_be(p_report->azimuth_rad, &out_frame->payload[16]);
    write_f32_be(p_report->range_rate_mps, &out_frame->payload[20]);
    write_f32_be(p_report->azimuth_rate_radps, &out_frame->payload[24]);
    out_frame->payload[28] = p_report->state;
    out_frame->payload[29] = p_report->result;
    out_frame->payload[30] = p_report->flags;
    out_frame->payload[31] = p_report->reserved;

    return 0;
}
