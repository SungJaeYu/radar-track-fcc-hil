#include "tracking/track.h"

// Lifecycle thresholds are independent of the configurable measurement gates.
#define TRACK_CONFIRM_HITS 3U
#define TRACK_LOST_MISSES 5U
// Sensor timestamp gap limit, distinct from the host-clock RX silence timeout.
#define TRACK_MAX_PREDICT_INTERVAL_MS 1000U

void track_init(struct track *p_track)
{
    p_track->state = TRACK_STATE_EMPTY;
    p_track->hit_count = 0U;
    p_track->miss_count = 0U;
    p_track->estimate_timestamp_ms = 0U;
}

void track_start(struct track *p_track, float initial_range_m, float initial_azimuth_rad, uint32_t timestamp_ms)
{
    estimator_init(&p_track->estimator, initial_range_m, initial_azimuth_rad);

    p_track->state = TRACK_STATE_TENTATIVE;
    p_track->hit_count = 1U;
    p_track->miss_count = 0U;

    p_track->estimate_timestamp_ms = timestamp_ms;
}

TrackPredictResult track_predict(struct track *p_track, uint32_t timestamp_ms)
{
    if (p_track->state == TRACK_STATE_EMPTY) {
        return TRACK_PREDICT_RESULT_EMPTY;
    }

    // Discard a lost track before checking its old timestamp.
    // The caller starts a new track on the next measurement.
    if (p_track->state == TRACK_STATE_LOST) {
        track_init(p_track);
        return TRACK_PREDICT_RESULT_RESET;
    }

    // Timestamp of the current filter state, including prediction-only updates.
    const uint32_t delta_ms = timestamp_ms - p_track->estimate_timestamp_ms;

    // Reject duplicate or backward/ambiguous timestamps.
    // Unsigned subtraction allows normal uint32 timestamp wraparound.
    if (delta_ms == 0U || delta_ms >= UINT32_C(0x80000000)) {
        return TRACK_PREDICT_RESULT_TIME_REJECTED;
    }

    // A long forward gap invalidates the current track.
    if (delta_ms > TRACK_MAX_PREDICT_INTERVAL_MS) {
        track_init(p_track);
        return TRACK_PREDICT_RESULT_RESET;
    }

    const float dt_s = (float)delta_ms / 1000.0f;

    estimator_predict(&p_track->estimator, dt_s);

    p_track->estimate_timestamp_ms = timestamp_ms;

    return TRACK_PREDICT_RESULT_PREDICTED;
}

void track_hit(struct track *p_track, float measured_range_m, float measured_azimuth_rad)
{
    if (p_track->state == TRACK_STATE_EMPTY || p_track->state == TRACK_STATE_LOST) {
        return;
    }

    estimator_correct(&p_track->estimator, measured_range_m, measured_azimuth_rad);

    if (p_track->hit_count < TRACK_CONFIRM_HITS) {
        p_track->hit_count++;
    }
    p_track->miss_count = 0U;

    switch (p_track->state) {
    case TRACK_STATE_TENTATIVE:
        if (p_track->hit_count >= TRACK_CONFIRM_HITS) {
            p_track->state = TRACK_STATE_CONFIRMED;
        }
        break;
    case TRACK_STATE_CONFIRMED:
        break;
    case TRACK_STATE_COASTING:
        p_track->state = TRACK_STATE_CONFIRMED;
        break;
    default:
        break;
    }
}

void track_miss(struct track *p_track)
{
    if (p_track->state == TRACK_STATE_EMPTY || p_track->state == TRACK_STATE_LOST) {
        return;
    }

    if (p_track->state == TRACK_STATE_TENTATIVE) {
        track_init(p_track);
        return;
    }

    p_track->hit_count = 0U;
    if (p_track->miss_count < TRACK_LOST_MISSES) {
        p_track->miss_count++;
    }

    if (p_track->state == TRACK_STATE_CONFIRMED) {
        p_track->state = TRACK_STATE_COASTING;
    }

    if (p_track->miss_count >= TRACK_LOST_MISSES) {
        p_track->state = TRACK_STATE_LOST;
    }
}
