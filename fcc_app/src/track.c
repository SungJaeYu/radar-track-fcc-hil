#include "track.h"

#define TRACK_CONFIRM_HITS 3U
#define TRACK_LOST_MISSES 5U
#define TRACK_MAX_PREDICT_INTERVAL_MS 1000U

void track_init(struct track *p_track)
{
    p_track->state = TRACK_EMPTY;
    p_track->hit_count = 0U;
    p_track->miss_count = 0U;
    p_track->last_measurement_time_ms = 0U;
}

void track_start(struct track *p_track, float initial_range, float initial_azimuth_rad, uint32_t timestamp_ms)
{
    kalman_cv_init(
        &p_track->range_filter,
        initial_range,      // initial_range
        0.0f,      // initial_range_rate
        25.0f,     // p00: range uncertainty
        0.0f,      // p01
        0.0f,      // p10
        10000.0f,  // p11: range-rate uncertainty
        100.0f,    // q: acceleration noise variance
        25.0f      // R: measurement noise variance
    );

    kalman_cv_init(
        &p_track->azimuth_filter,
        initial_azimuth_rad,      // initial_azimuth
        0.0f,      // initial_azimuth_rate
        0.00121847f,     // p00: azimuth uncertainty: std dev = 2 deg, p00 = (2 * PI / 180)^2 [rad^2]
        0.0f,      // p01
        0.0f,      // p10
        0.03046174f,  // p11: angular velocity uncertainty: std dev = 10 deg/s, p11 = (10 * PI / 180)^2 [(rad/s)^2]
        0.00761544f,    // q: angular acceleration noise variance: std dev = 5 deg/s^2, q = (5 * PI / 180)^2 [(rad/s^2)^2]
        0.00121847f      // R: measurement noise variance: std dev = 2 deg, R = (2 * PI / 180)^2 [rad^2]
    );

    p_track->state = TRACK_TENTATIVE;
    p_track->hit_count = 1U;
    p_track->miss_count = 0U;

    p_track->last_measurement_time_ms = timestamp_ms;
}

bool track_predict(struct track *p_track, uint32_t timestamp_ms)
{
    if (p_track->state == TRACK_EMPTY) {
        return false;
    }

    // Discard a lost track before checking ists old timestamp.
    // The caller starts a new track on the next measurement.
    if (p_track->state == TRACK_LOST) {
        track_init(p_track);
        return false;
    }

    // Timestamp of the current filter state, including prediction-only updates.
    const uint32_t delta_ms = timestamp_ms - p_track->last_measurement_time_ms;

    // Reject duplicate or backward/ambiguous timestamps.
    // Unsigned subtraction allows normal uint32 timestamp wraparound.
    if (delta_ms == 0U || delta_ms >= UINT32_C(0x80000000)) {
        return false;
    }

    // A long forward gap invalidates the current track.
    if (delta_ms > TRACK_MAX_PREDICT_INTERVAL_MS) {
        track_init(p_track);
        return false;
    }

    const float dt = (float)delta_ms / 1000.0f;

    kalman_cv_predict(
        &p_track->range_filter,
        dt
    );

    kalman_cv_predict_angle(
        &p_track->azimuth_filter,
        dt
    );

    p_track->last_measurement_time_ms = timestamp_ms;
    
    return true;
}

void track_hit(struct track *p_track, float measured_range, float measured_azimuth_rad)
{
    if (p_track->state == TRACK_EMPTY || p_track->state == TRACK_LOST) {
        return;
    }

    kalman_cv_correct(&p_track->range_filter, measured_range);
    kalman_cv_correct_angle(&p_track->azimuth_filter, measured_azimuth_rad);

    if (p_track->hit_count < TRACK_CONFIRM_HITS) {
        p_track->hit_count++;
    }
    p_track->miss_count = 0U;

    switch (p_track->state)
    {
    case TRACK_TENTATIVE:
        if (p_track->hit_count >= TRACK_CONFIRM_HITS) {
            p_track->state = TRACK_CONFIRMED;
        }
        break;
    case TRACK_CONFIRMED:
        break;
    case TRACK_COASTING:
        p_track->state = TRACK_CONFIRMED;
        break;
    default:
        break;
    }
}

void track_miss(struct track *p_track)
{
    if (p_track->state == TRACK_EMPTY || p_track->state == TRACK_LOST) {
        return;
    }

    if (p_track->state == TRACK_TENTATIVE) {
        track_init(p_track);
        return;
    }

    p_track->hit_count = 0U;
    if (p_track->miss_count < TRACK_LOST_MISSES) {
        p_track->miss_count++;
    }

    if (p_track->state == TRACK_CONFIRMED) {
        p_track->state = TRACK_COASTING;
    }
    
    if (p_track->miss_count >= TRACK_LOST_MISSES) {
        p_track->state = TRACK_LOST;
    }
}
