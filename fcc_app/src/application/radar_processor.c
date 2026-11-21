#include "application/radar_processor.h"

#include <zephyr/logging/log.h>
#include <zephyr/sys/__assert.h>

#include "protocol/measurement_message.h"
#include "tracking/gating.h"

// Sensor timestamp gap limit, distinct from the board-clock RX silence timeout.
#define TRACK_MAX_PREDICTION_INTERVAL_MS 1000U

// Processing stays in the handler thread and shares its logging configuration.
LOG_MODULE_DECLARE(frame_handler, CONFIG_RADAR_HANDLER_LOG_LEVEL);

static TrackReportResult update_track_from_measurement(struct track *p_track, const struct radar_measurement *p_measurement);
static void log_track_started(const struct radar_measurement *p_measurement);
static void log_prediction(const struct estimator_output *p_estimate);
static void log_prediction_skipped(uint32_t previous_estimate_sensor_ms, uint32_t input_sensor_ms, TrackState state);
static void log_track_update(const struct track *p_track, bool measurement_accepted);
static const char *track_state_name(TrackState state);

void radar_processor_reset(struct radar_processor *p_processor)
{
    track_init(&p_processor->track);

    // Arm timeout monitoring only after the next valid radar measurement.
    p_processor->last_valid_rx_uptime_ms = 0;
    p_processor->timeout_armed = false;
}

bool radar_processor_is_timed_out(const struct radar_processor *p_processor, int64_t now_uptime_ms)
{
    // Compare board uptime with RX completion time; querying has no side effects.
    return p_processor->timeout_armed &&
           now_uptime_ms - p_processor->last_valid_rx_uptime_ms >= CONFIG_RADAR_MEASUREMENT_TIMEOUT_MS;
}

bool radar_processor_process_measurement(struct radar_processor *p_processor,
                                         const struct protocol_frame *p_frame, int64_t received_uptime_ms,
                                         struct radar_process_result *out_result)
{
    struct radar_measurement measurement;

    const bool payload_decoded = measurement_message_decode(p_frame->payload, p_frame->payload_length, &measurement);
    if (!payload_decoded) {
        LOG_WRN("Invalid radar payload: length_bytes=%u", (unsigned int)p_frame->payload_length);
        return false;
    }

    const bool measurement_valid = measurement_message_validate(&measurement);
    if (!measurement_valid) {
        LOG_WRN("Invalid radar measurement: range_m=%.2f azimuth_rad=%.4f",
                (double)measurement.range_m, (double)measurement.azimuth_rad);
        return false;
    }

    // Record received time for valid radar payloads, before timestamp/gate checks.
    // A later tracking rejection still counts as receiving a valid measurement payload.
    p_processor->last_valid_rx_uptime_ms = received_uptime_ms;
    p_processor->timeout_armed = true;

    LOG_DBG("Measurement: sensor_ms=%u range_m=%.2f azimuth_rad=%.4f",
            (unsigned int)measurement.sensor_time_ms, (double)measurement.range_m, (double)measurement.azimuth_rad);

    const TrackState previous_state = p_processor->track.state;
    const TrackReportResult result = update_track_from_measurement(&p_processor->track, &measurement);

    // Startup has its own log; report other state transitions only when they occur.
    if (previous_state != TRACK_STATE_EMPTY && previous_state != p_processor->track.state) {
        LOG_INF("Track state: previous=%s current=%s",
                track_state_name(previous_state), track_state_name(p_processor->track.state));
    }

    out_result->input_sensor_ms = measurement.sensor_time_ms;
    out_result->result = result;
    return true;
}

static TrackReportResult update_track_from_measurement(struct track *p_track, const struct radar_measurement *p_measurement)
{
    const uint32_t previous_estimate_sensor_ms = p_track->estimate_sensor_ms;

    switch (p_track->state) {
    case TRACK_STATE_EMPTY:
        track_start(p_track, (float)p_measurement->range_m, (float)p_measurement->azimuth_rad, p_measurement->sensor_time_ms);
        log_track_started(p_measurement);
        return TRACK_REPORT_RESULT_STARTED;

    case TRACK_STATE_LOST:
        // Reset before checking the old timestamp; start anew on the next measurement.
        track_init(p_track);
        log_prediction_skipped(previous_estimate_sensor_ms, p_measurement->sensor_time_ms, p_track->state);
        return TRACK_REPORT_RESULT_RESET;

    case TRACK_STATE_TENTATIVE:
    case TRACK_STATE_CONFIRMED:
    case TRACK_STATE_COASTING: {
        // Unsigned subtraction allows normal uint32 timestamp wraparound.
        const uint32_t prediction_interval_ms = p_measurement->sensor_time_ms - previous_estimate_sensor_ms;
        if (prediction_interval_ms == 0U || prediction_interval_ms >= TRACK_SENSOR_HALF_RANGE_MS) {
            log_prediction_skipped(previous_estimate_sensor_ms, p_measurement->sensor_time_ms, p_track->state);
            return TRACK_REPORT_RESULT_TIME_REJECTED;
        }

        if (prediction_interval_ms > TRACK_MAX_PREDICTION_INTERVAL_MS) {
            // Do not reuse this measurement to start the cleared track.
            track_init(p_track);
            log_prediction_skipped(previous_estimate_sensor_ms, p_measurement->sensor_time_ms, p_track->state);
            return TRACK_REPORT_RESULT_RESET;
        }

        const struct track next = track_predict_next(p_track, p_measurement->sensor_time_ms);
        *p_track = next;

        const struct estimator_output estimate = track_get_estimate(p_track);
        log_prediction(&estimate);

        const bool accepted = gating_check(
            p_measurement->range_m,
            estimate.range_m,
            p_measurement->azimuth_rad,
            estimate.azimuth_rad
        );

        // A gate miss keeps the prediction and only advances lifecycle policy.
        if (accepted) {
            track_apply_measurement(p_track, p_measurement->range_m, p_measurement->azimuth_rad);
        } else {
            track_apply_miss(p_track);
        }
        log_track_update(p_track, accepted);
        return accepted ? TRACK_REPORT_RESULT_UPDATED : TRACK_REPORT_RESULT_GATE_REJECTED;
    }

    default:
        __ASSERT(false, "Invalid track state: %d", (int)p_track->state);
        // Clear invalid state when assertions are disabled.
        track_init(p_track);
        return TRACK_REPORT_RESULT_RESET;
    }
}

static void log_prediction(const struct estimator_output *p_estimate)
{
    LOG_DBG("Prediction: range_m=%.2f azimuth_rad=%.4f",
            (double)p_estimate->range_m, (double)p_estimate->azimuth_rad);
}

static void log_track_started(const struct radar_measurement *p_measurement)
{
    LOG_INF("Track started: state=TENTATIVE sensor_ms=%u range_m=%.2f azimuth_rad=%.4f",
            (unsigned int)p_measurement->sensor_time_ms,
            (double)p_measurement->range_m, (double)p_measurement->azimuth_rad);
}

static void log_prediction_skipped(uint32_t previous_estimate_sensor_ms, uint32_t input_sensor_ms, TrackState state)
{
    LOG_DBG("Prediction skipped: previous_estimate_sensor_ms=%u input_sensor_ms=%u state=%s",
            (unsigned int)previous_estimate_sensor_ms,
            (unsigned int)input_sensor_ms, track_state_name(state));
}

static void log_track_update(const struct track *p_track, bool measurement_accepted)
{
    if (p_track->state == TRACK_STATE_TENTATIVE ||
        p_track->state == TRACK_STATE_CONFIRMED ||
        p_track->state == TRACK_STATE_COASTING) {
        const struct estimator_output updated = track_get_estimate(p_track);

        LOG_DBG("%s: range_m=%.2f azimuth_rad=%.4f range_rate_mps=%.2f azimuth_rate_radps=%.4f",
                measurement_accepted ? "Measurement accepted" : "Measurement rejected by gate",
                (double)updated.range_m,
                (double)updated.azimuth_rad,
                (double)updated.range_rate_mps,
                (double)updated.azimuth_rate_radps);
    } else {
        LOG_DBG("%s: state=%s estimate=invalid",
                measurement_accepted ? "Measurement accepted" : "Measurement rejected by gate",
                track_state_name(p_track->state));
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
