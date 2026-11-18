#include "application/report_sender.h"

#include <errno.h>

#include <zephyr/logging/log.h>

#include "protocol/status_message.h"
#include "transport/rx_frame_processor.h"
#include "transport/uart_rx.h"
#include "transport/uart_tx.h"

// Reports run in the handler context and share its logging configuration.
LOG_MODULE_DECLARE(frame_handler, CONFIG_RADAR_HANDLER_LOG_LEVEL);

void report_sender_submit_status(uint32_t stale_measurements)
{
    struct rx_frame_processor_stats stats;
    rx_frame_processor_get_stats(&stats);

    struct tx_frame request;
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
        .stale_measurements = stale_measurements,
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

void report_sender_submit_track(struct report_sender *p_sender, const struct track *p_track,
                                uint32_t input_timestamp_ms, bool input_present, TrackReportResult result)
{
    struct tx_frame request;

    const bool estimate_valid =
        p_track->state == TRACK_STATE_TENTATIVE ||
        p_track->state == TRACK_STATE_CONFIRMED ||
        p_track->state == TRACK_STATE_COASTING;

    struct track_report report = {
        .report_sequence = p_sender->next_track_report_sequence++,
        .input_timestamp_ms = input_present ? input_timestamp_ms : 0,
        .state = p_track->state,
        .result = result,
        .flags = input_present ? TRACK_REPORT_FLAG_INPUT_PRESENT : 0,
        .reserved = 0,
    };

    // Invalid estimates retain zero-valued fields without reading stale filters.
    if (estimate_valid) {
        report.estimate_timestamp_ms = p_track->estimate_timestamp_ms;
        report.range_m = p_track->range_filter.estimate;
        report.azimuth_rad = p_track->azimuth_filter.estimate;
        report.range_rate_mps = p_track->range_filter.rate;
        report.azimuth_rate_radps = p_track->azimuth_filter.rate;
        report.flags |= TRACK_REPORT_FLAG_ESTIMATE_VALID;
    }

    int ret = track_message_encode(&report, &request.frame);
    if (ret != 0) {
        LOG_ERR("TRACK encode failed: %d", ret);
        return;
    }
    ret = uart_tx_submit(&request);
    if (ret == -ENOMSG) {
        LOG_WRN("TRACK submit failed: %d (TX queue full)", ret);
        return;
    }
    if (ret != 0) {
        LOG_ERR("TRACK submit failed: %d", ret);
        return;
    }
    // Submission only confirms a queue copy, not transmission or peer reception.
    LOG_DBG("TRACK report queued: payload=%u bytes", (unsigned int)request.frame.payload_length);
}
