#include "application/frame_handler.h"

#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "protocol/radar_message.h"
#include "tracking/gating.h"
#include "transport/rx_frame_processor.h"

// Expire the track after 1000 ms without a decoded and validated radar measurement.
#define RADAR_RX_TIMEOUT_MS 1000

static void frame_handler_thread(void *p_arg1, void *p_arg2, void *p_arg3);

static void handle_radar_measurement(struct frame_handler *p_handler, const struct received_frame *p_frame);
static void update_track_from_measurement(struct track *p_track, const struct radar_measurement *p_measurement);

K_THREAD_DEFINE(frame_handler_tid, 2048, frame_handler_thread, NULL, NULL, NULL, 6, 0, 0);

// Requests are copied into this queue; only the handler thread updates its track.
K_MSGQ_DEFINE(frame_queue, sizeof(struct received_frame), 8, 4);

void frame_handler_init(struct frame_handler *p_handler)
{
    track_init(&p_handler->track);

    // Arm timeout monitoring only after the next valid radar measurement.
    p_handler->last_valid_rx_ms = 0;
    p_handler->timeout_armed = false;
}

static void frame_handler_thread(void *p_arg1, void *p_arg2, void *p_arg3)
{
    ARG_UNUSED(p_arg1);
    ARG_UNUSED(p_arg2);
    ARG_UNUSED(p_arg3);

    // Initialize handler state before processing queued frames.
    struct frame_handler handler;
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
    // No timeout before the first valid measurement after initialization or reset.
    if (!p_handler->timeout_armed) {
        return;
    }

    // Both times use board uptime in ms, not the sender's measurement timestamp.
    const int64_t elapsed_ms = now_ms - p_handler->last_valid_rx_ms;

    if (elapsed_ms < RADAR_RX_TIMEOUT_MS) {
        return;
    }

    // Clear the stale track and disarm monitoring so this timeout is logged once.
    frame_handler_reset(p_handler);
    printk("Radar measurement timeout: track cleared\n");
}

void handle_frame(struct frame_handler *p_handler, const struct received_frame *p_frame)
{
    switch (p_frame->frame.type) {
    case FRAME_TYPE_INIT:
        printk("TYPE_INIT\n");
        frame_handler_init(p_handler);
        break;

    case FRAME_TYPE_MEAS_REPORT: {
        printk("TYPE_MEAS\n");
        handle_radar_measurement(p_handler, p_frame);
        break;
    }
    // Diagnostics are currently console-only; no UART status response is submitted.
    case FRAME_TYPE_STATUS_REQUEST: {
        printk("TYPE_STATUS\n");
        struct rx_frame_stats stats;
        rx_frame_processor_get_stats(&stats);
        printk("CRC: %u, length: %u, timeout: %u, queue drops: %u\n",
            (unsigned int)stats.crc_errors,
            (unsigned int)stats.length_errors,
            (unsigned int)stats.frame_timeouts,
            (unsigned int)stats.queue_drops);
        break;
    }

    // Placeholder: no command payload interpretation or control action yet.
    case FRAME_TYPE_COMMAND:
        printk("TYPE_COMMAND\n");
        break;

    case FRAME_TYPE_RESET:
        printk("TYPE RESET\n");
        frame_handler_reset(p_handler);
        break;
    default:
        printk("Unknown frame type: 0x%02X\n", p_frame->frame.type);
        break;
    }
}

static void handle_radar_measurement(struct frame_handler *p_handler, const struct received_frame *p_frame)
{
    struct radar_measurement rm;

    if (!decode_radar_measurement(
            p_frame->frame.payload,
            p_frame->frame.payload_length,
            &rm)) {
        printk("Invalid radar payload\n");
        return;
    }

    if (!validate_radar_measurement(&rm)) {
        printk("Invalid radar measurement\n");
        return;
    }

    // Record received time for valid radar payloads, before timestamp/gate checks.
    // A later tracking rejection still counts as receiving a valid measurement payload.
    p_handler->last_valid_rx_ms = p_frame->received_at_ms;
    p_handler->timeout_armed = true;

    printk("range: %.2f\n", (double)rm.range_m);
    printk("azimuth: %.2f\n", (double)rm.azimuth_rad);

    update_track_from_measurement(&p_handler->track, &rm);

}

static void update_track_from_measurement(struct track *p_track, const struct radar_measurement *p_measurement)
{
    if (p_track->state == TRACK_STATE_EMPTY) {
        track_start(p_track, (float)p_measurement->range_m, (float)p_measurement->azimuth_rad, p_measurement->timestamp_ms);

        printk("Filters initialized at range: %.2f\n", (double)p_measurement->range_m);
        printk("Filters initialized at azimuth: %.2f\n", (double)p_measurement->azimuth_rad);
    } else {

        // Prediction may clear a lost/stale track. A later measurement starts it anew;
        // this measurement is not reused for startup after prediction returns false.
        if (!track_predict(p_track, p_measurement->timestamp_ms)) {
            printk("Invalid measurement interval\n");
            return;
        }

        const float predicted_range_m = p_track->range_filter.estimate;
        const float predicted_azimuth_rad = p_track->azimuth_filter.estimate;

        printk("predicted range: %.2f\n", (double)predicted_range_m);
        printk("predicted azimuth: %.2f\n", (double)predicted_azimuth_rad);

        // Gating
        const bool is_in_gate = gating_check(
            p_measurement->range_m,
            predicted_range_m,
            p_measurement->azimuth_rad,
            predicted_azimuth_rad
        );

        // Correction
        if (is_in_gate) {
            track_hit(p_track, p_measurement->range_m, p_measurement->azimuth_rad);
            printk("Measurement accepted\n");
        } else {
            track_miss(p_track);
            printk("Measurement rejected by gate\n");
        }

        const float filtered_range_m =
            p_track->range_filter.estimate;

        const float filtered_azimuth_rad =
            p_track->azimuth_filter.estimate;

        printk("filtered range: %.2f\n",
            (double)filtered_range_m);

        printk("filtered azimuth: %.2f\n",
            (double)filtered_azimuth_rad);

        printk("estimated range rate: %.2f\n",
            (double)p_track->range_filter.rate);
        printk("estimated azimuth rate: %.2f\n",
            (double)p_track->azimuth_filter.rate);
    }
}

int frame_handler_submit(const struct received_frame *p_frame)
{
    if (p_frame == NULL || !protocol_frame_is_valid(&p_frame->frame)) {
        return -EINVAL;
    }

    return k_msgq_put(&frame_queue, p_frame, K_NO_WAIT);
}
