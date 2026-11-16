#include "application/frame_handler.h"

#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

static void frame_handler_thread(void *p_arg1, void *p_arg2, void *p_arg3);

LOG_MODULE_REGISTER(frame_handler, CONFIG_RADAR_HANDLER_LOG_LEVEL);

K_THREAD_DEFINE(frame_handler_tid, 2048, frame_handler_thread, NULL, NULL, NULL, 6, 0, 0);

// Requests are copied into this queue; only the handler thread updates its track.
K_MSGQ_DEFINE(frame_queue, sizeof(struct received_frame), 8, 4);

void frame_handler_init(struct frame_handler *p_handler)
{
    radar_processor_reset(&p_handler->radar);
}

static void frame_handler_thread(void *p_arg1, void *p_arg2, void *p_arg3)
{
    ARG_UNUSED(p_arg1);
    ARG_UNUSED(p_arg2);
    ARG_UNUSED(p_arg3);

    // Zero-initialize the handler before its first use.
    // Preserve the report sequence across initialization and reset.
    struct frame_handler handler = {0};
    struct received_frame frame;

    frame_handler_init(&handler);

    while (1) {
        // Wake periodically even without a frame so the no-measurement timeout can expire.
        const int ret = k_msgq_get(&frame_queue, &frame, K_MSEC(50));

        if (ret == 0) {
            // Process the received frame in the track owner thread.
            handle_frame(&handler, &frame);
        }

        frame_handler_check_timeout(&handler, k_uptime_get());
    }
}

void frame_handler_reset(struct frame_handler *p_handler)
{
    frame_handler_init(p_handler);
}

void frame_handler_check_timeout(struct frame_handler *p_handler, int64_t now_ms)
{
    // Event reporting is not connected yet; preserve the existing reset-only behavior.
    (void)radar_processor_check_timeout(&p_handler->radar, now_ms);
}

void handle_frame(struct frame_handler *p_handler, const struct received_frame *p_frame)
{
    switch (p_frame->frame.type) {
    case FRAME_TYPE_INIT:
        LOG_INF("INIT: track and measurement monitoring reset");
        frame_handler_init(p_handler);
        break;

    case FRAME_TYPE_MEAS_REPORT: {
        struct radar_process_result result;
        if (radar_processor_process(&p_handler->radar, &p_frame->frame,
                                    p_frame->received_at_ms, &result)) {
            report_sender_submit_track(&p_handler->report_sender, &p_handler->radar.track,
                                       result.input_timestamp_ms, result.result);
        }
        break;
    }
    // Queue a STATUS_REPORT containing the current RX diagnostic counters.
    case FRAME_TYPE_STATUS_REQUEST: {
        report_sender_submit_status();
        break;
    }
    // Placeholder: no command payload interpretation or control action yet.
    case FRAME_TYPE_COMMAND:
        LOG_WRN("COMMAND ignored: not implemented");
        break;
    case FRAME_TYPE_RESET:
        LOG_INF("RESET: track and measurement monitoring reset");
        frame_handler_reset(p_handler);
        break;
    case FRAME_TYPE_STATUS_REPORT:
        LOG_WRN("STATUS_REPORT ignored: unexpected input");
        break;
    case FRAME_TYPE_TRACK_REPORT:
        LOG_WRN("TRACK_REPORT ignored: unexpected input");
        break;
    default:
        LOG_WRN("Frame ignored: unsupported type=0x%02X", p_frame->frame.type);
        break;
    }
}

int frame_handler_submit(const struct received_frame *p_frame)
{
    if (p_frame == NULL || !protocol_frame_is_valid(&p_frame->frame)) {
        return -EINVAL;
    }

    return k_msgq_put(&frame_queue, p_frame, K_NO_WAIT);
}
