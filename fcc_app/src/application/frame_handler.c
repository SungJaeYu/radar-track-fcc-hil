#include "application/frame_handler.h"

#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "protocol/radar_message.h"
#include "protocol/status_message.h"
#include "tracking/gating.h"
#include "transport/rx_frame_processor.h"
#include "transport/uart_rx.h"
#include "transport/uart_tx.h"

static void frame_handler_thread(void *p_arg1, void *p_arg2, void *p_arg3);

static void handle_radar_measurement(struct frame_handler *p_handler, const struct received_frame *p_frame);
static void update_track_from_measurement(struct track *p_track, const struct radar_measurement *p_measurement);
static void handle_status_request(void);
static const char *track_state_name(TrackState state);

LOG_MODULE_REGISTER(frame_handler, CONFIG_RADAR_HANDLER_LOG_LEVEL);

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

    if (elapsed_ms < CONFIG_RADAR_MEASUREMENT_TIMEOUT_MS) {
        return;
    }

    // Clear the stale track and disarm monitoring so this timeout is logged once.
    frame_handler_reset(p_handler);

    LOG_WRN("Radar measurement timeout: track cleared");
}

void handle_frame(struct frame_handler *p_handler, const struct received_frame *p_frame)
{
    switch (p_frame->frame.type) {
    case FRAME_TYPE_INIT:
        LOG_INF("INIT: track and measurement monitoring reset");
        frame_handler_init(p_handler);
        break;

    case FRAME_TYPE_MEAS_REPORT: {
        handle_radar_measurement(p_handler, p_frame);
        break;
    }
    // Queue a STATUS_REPORT containing the current RX diagnostic counters.
    case FRAME_TYPE_STATUS_REQUEST: {
        handle_status_request();
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
    default:
        LOG_WRN("Frame ignored: unsupported type=0x%02X", p_frame->frame.type);
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
        LOG_WRN("Invalid radar payload: length=%u", (unsigned int)p_frame->frame.payload_length);
        return;
    }

    if (!validate_radar_measurement(&rm)) {
        LOG_WRN("Invalid radar measurement: range=%.2f m, azimuth=%.4f rad",
                (double)rm.range_m, (double)rm.azimuth_rad);
        return;
    }

    // Record received time for valid radar payloads, before timestamp/gate checks.
    // A later tracking rejection still counts as receiving a valid measurement payload.
    p_handler->last_valid_rx_ms = p_frame->received_at_ms;
    p_handler->timeout_armed = true;

    LOG_DBG("Measurement: t=%u ms, range=%.2f m, azimuth=%.4f rad",
            (unsigned int)rm.timestamp_ms, (double)rm.range_m, (double)rm.azimuth_rad);

    const TrackState previous_state = p_handler->track.state;
    update_track_from_measurement(&p_handler->track, &rm);
    // Startup has its own log; report other state transitions only when they occur.
    if (previous_state != TRACK_STATE_EMPTY && previous_state != p_handler->track.state) {
        LOG_INF("Track state: %s -> %s",
                track_state_name(previous_state), track_state_name(p_handler->track.state));
    }

}

static void update_track_from_measurement(struct track *p_track, const struct radar_measurement *p_measurement)
{
    if (p_track->state == TRACK_STATE_EMPTY) {
        track_start(p_track, (float)p_measurement->range_m, (float)p_measurement->azimuth_rad, p_measurement->timestamp_ms);

        LOG_INF("Track started: TENTATIVE, t=%u ms, range=%.2f m, azimuth=%.4f rad",
                (unsigned int)p_measurement->timestamp_ms,
                (double)p_measurement->range_m, (double)p_measurement->azimuth_rad);
    } else {

        // Prediction may clear a lost/stale track. A later measurement starts it anew;
        // this measurement is not reused for startup after prediction returns false.
        const uint32_t previous_time_ms = p_track->last_measurement_time_ms;
        if (!track_predict(p_track, p_measurement->timestamp_ms)) {
            LOG_DBG("Prediction skipped: previous_t=%u ms, received_t=%u ms, state=%s",
                    (unsigned int)previous_time_ms,
                    (unsigned int)p_measurement->timestamp_ms, track_state_name(p_track->state));
            return;
        }

        const float predicted_range_m = p_track->range_filter.estimate;
        const float predicted_azimuth_rad = p_track->azimuth_filter.estimate;

        LOG_DBG("Prediction: range=%.2f m, azimuth=%.4f rad",
                (double)predicted_range_m, (double)predicted_azimuth_rad);

        // Compare with the prediction using both configured inclusive gates.
        const bool is_in_gate = gating_check(
            p_measurement->range_m,
            predicted_range_m,
            p_measurement->azimuth_rad,
            predicted_azimuth_rad
        );

        // Only accepted measurements correct the filters. A gate miss retains
        // the prediction and advances track-loss policy, not RX error statistics.
        if (is_in_gate) {
            track_hit(p_track, p_measurement->range_m, p_measurement->azimuth_rad);
        } else {
            track_miss(p_track);
        }

        LOG_DBG("%s: range=%.2f m, azimuth=%.4f rad, range_rate=%.2f m/s, azimuth_rate=%.4f rad/s",
                is_in_gate ? "Measurement accepted" : "Measurement rejected by gate",
                (double)p_track->range_filter.estimate,
                (double)p_track->azimuth_filter.estimate,
                (double)p_track->range_filter.rate,
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

static void handle_status_request(void)
{
    struct rx_frame_stats stats;
    rx_frame_processor_get_stats(&stats);

    struct send_frame request;
    struct uart_rx_stats uart_stats;
    uart_rx_get_stats(&uart_stats);
    // Each owner supplies cumulative counters; this is not a simultaneous snapshot.
    const struct status_report report = {
        .crc_errors = stats.crc_errors,
        .length_errors = stats.length_errors,
        .frame_timeouts = stats.frame_timeouts,
        .queue_drops = stats.queue_drops,
        .ring_overflow_events = uart_stats.ring_overflow_events,
        .rx_dropped_bytes = uart_stats.rx_dropped_bytes,
        .recovery_discarded_bytes = uart_stats.recovery_discarded_bytes,
    };
    int ret = status_message_encode(&report, &request.frame);
    if (ret != 0) {
        LOG_ERR("STATUS encode failed: %d", ret);
        return;
    }
    ret = uart_tx_submit(&request);
    if (ret == -ENOMSG) {
        LOG_WRN("STATUS submit failed: %d (TX queue full)", ret);
        return;
    }
    if (ret != 0) {
        LOG_ERR("STATUS submit failed: %d", ret);
        return;
    }
    // Submission only confirms a queue copy, not transmission or peer reception.
    LOG_DBG("STATUS report queued: payload=%u bytes", (unsigned int)request.frame.payload_length);
}

static const char *track_state_name(TrackState state)
{
    switch (state) {
    case TRACK_STATE_EMPTY:
        return "EMPTY";
    case TRACK_STATE_TENTATIVE:
        return "TENTATIVE";
    case TRACK_STATE_CONFIRMED:
        return "CONFIRMED";
    case TRACK_STATE_COASTING:
        return "COASTING";
    case TRACK_STATE_LOST:
        return "LOST";
    default:
        return "UNKNOWN";
    }
}
