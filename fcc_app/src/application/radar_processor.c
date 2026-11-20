#include "application/radar_processor.h"

#include <zephyr/logging/log.h>

#include "protocol/measurement_message.h"
#include "tracking/gating.h"

// Processing stays in the handler thread and shares its logging configuration.
LOG_MODULE_DECLARE(frame_handler, CONFIG_RADAR_HANDLER_LOG_LEVEL);

static TrackReportResult update_track_from_measurement(struct track *p_track,
                                                       const struct radar_measurement *p_measurement);
static const char *track_state_name(TrackState state);

void radar_processor_reset(struct radar_processor *p_processor)
{
    track_init(&p_processor->track);

    // Arm timeout monitoring only after the next valid radar measurement.
    p_processor->last_valid_rx_ms = 0;
    p_processor->timeout_armed = false;
}

bool radar_processor_is_timed_out(const struct radar_processor *p_processor, int64_t now_ms)
{
    // Compare board uptime with RX completion time; querying has no side effects.
    return p_processor->timeout_armed &&
           now_ms - p_processor->last_valid_rx_ms >= CONFIG_RADAR_MEASUREMENT_TIMEOUT_MS;
}

bool radar_processor_process_measurement(struct radar_processor *p_processor,
                                         const struct protocol_frame *p_frame, int64_t received_at_ms,
                                         struct radar_process_result *out_result)
{
    struct radar_measurement measurement;

    if (!measurement_message_decode(
            p_frame->payload,
            p_frame->payload_length,
            &measurement)) {
        LOG_WRN("Invalid radar payload: length=%u", (unsigned int)p_frame->payload_length);
        return false;
    }

    if (!measurement_message_validate(&measurement)) {
        LOG_WRN("Invalid radar measurement: range=%.2f m, azimuth=%.4f rad",
                (double)measurement.range_m, (double)measurement.azimuth_rad);
        return false;
    }

    // Record received time for valid radar payloads, before timestamp/gate checks.
    // A later tracking rejection still counts as receiving a valid measurement payload.
    p_processor->last_valid_rx_ms = received_at_ms;
    p_processor->timeout_armed = true;

    LOG_DBG("Measurement: t=%u ms, range=%.2f m, azimuth=%.4f rad",
            (unsigned int)measurement.timestamp_ms, (double)measurement.range_m, (double)measurement.azimuth_rad);

    const TrackState previous_state = p_processor->track.state;
    const TrackReportResult result = update_track_from_measurement(&p_processor->track, &measurement);

    // Startup has its own log; report other state transitions only when they occur.
    if (previous_state != TRACK_STATE_EMPTY && previous_state != p_processor->track.state) {
        LOG_INF("Track state: %s -> %s",
                track_state_name(previous_state), track_state_name(p_processor->track.state));
    }

    out_result->input_timestamp_ms = measurement.timestamp_ms;
    out_result->result = result;
    return true;
}

static TrackReportResult update_track_from_measurement(struct track *p_track, const struct radar_measurement *p_measurement)
{
    if (p_track->state == TRACK_STATE_EMPTY) {
        track_start(p_track, (float)p_measurement->range_m, (float)p_measurement->azimuth_rad, p_measurement->timestamp_ms);

        LOG_INF("Track started: TENTATIVE, t=%u ms, range=%.2f m, azimuth=%.4f rad",
                (unsigned int)p_measurement->timestamp_ms,
                (double)p_measurement->range_m, (double)p_measurement->azimuth_rad);

        return TRACK_REPORT_RESULT_STARTED;
    } else {

        // Prediction may clear a lost/stale track. A later measurement starts it anew;
        // this measurement is not reused for startup after a reset result.
        const uint32_t previous_time_ms = p_track->estimate_timestamp_ms;
        const TrackPredictResult prediction = track_predict(p_track, p_measurement->timestamp_ms);
        if (prediction != TRACK_PREDICT_RESULT_PREDICTED) {
            LOG_DBG("Prediction skipped: previous_t=%u ms, received_t=%u ms, state=%s",
                    (unsigned int)previous_time_ms,
                    (unsigned int)p_measurement->timestamp_ms, track_state_name(p_track->state));
            return prediction == TRACK_PREDICT_RESULT_TIME_REJECTED ?
                TRACK_REPORT_RESULT_TIME_REJECTED : TRACK_REPORT_RESULT_RESET;
        }

        const struct estimator_output predicted = estimator_get_output(&p_track->estimator);
        const float predicted_range_m = predicted.range_m;
        const float predicted_azimuth_rad = predicted.azimuth_rad;

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

        const struct estimator_output estimate = estimator_get_output(&p_track->estimator);
        LOG_DBG("%s: range=%.2f m, azimuth=%.4f rad, range_rate=%.2f m/s, azimuth_rate=%.4f rad/s",
                is_in_gate ? "Measurement accepted" : "Measurement rejected by gate",
                (double)estimate.range_m,
                (double)estimate.azimuth_rad,
                (double)estimate.range_rate_mps,
                (double)estimate.azimuth_rate_radps);

        (void)estimate; // LOG_DBG may compile out at the configured log level.

        return is_in_gate ? TRACK_REPORT_RESULT_UPDATED : TRACK_REPORT_RESULT_GATE_REJECTED;
    }
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
