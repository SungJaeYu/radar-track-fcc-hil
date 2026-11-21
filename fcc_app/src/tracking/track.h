#ifndef TRACK_H
#define TRACK_H

#include <stdint.h>
#include <stdbool.h>

#include "tracking/estimator.h"

// Deltas at or above half the uint32 timestamp range are backward or ambiguous.
#define TRACK_SENSOR_HALF_RANGE_MS UINT32_C(0x80000000)

// EMPTY: no active track; TENTATIVE: awaiting confirmation; CONFIRMED: established.
// COASTING: predicting after a gate miss; LOST: miss limit reached, awaiting reset.
typedef enum {
    TRACK_STATE_EMPTY,
    TRACK_STATE_TENTATIVE,
    TRACK_STATE_CONFIRMED,
    TRACK_STATE_COASTING,
    TRACK_STATE_LOST
} TrackState;

struct track {
    TrackState state;

    struct estimator estimator;

    uint32_t hit_count;
    uint32_t miss_count;

    // Sender timestamp of the current filter state, including prediction-only updates.
    uint32_t estimate_sensor_ms;
};

// All track APIs require a non-NULL object owned by the calling processing context.
// Clear state, counters, and timestamp; filter contents remain until track_start().
void track_init(struct track *p_track);

// Initialize filters from a measurement (m, rad, sender ms), enter TENTATIVE,
// and count this initial measurement as the first hit.
void track_start(
    struct track *p_track,
    float initial_range_m,
    float initial_azimuth_rad,
    uint32_t sensor_time_ms
);

// Predict an initialized TENTATIVE, CONFIRMED, or COASTING track without modifying it.
// The caller validates a positive forward timestamp delta below half the uint32 range
// and enforces its maximum-gap policy before calling. Normal timestamp wrap is supported.
// Return the complete next track, including covariance and the new timestamp.
// Lifecycle state and hit/miss counters are preserved; no rejection or reset occurs here.
struct track track_predict_next(const struct track *p_current, uint32_t target_sensor_ms);

// Return the current estimate by value without changing the track.
// The caller ensures the track is initialized and TENTATIVE, CONFIRMED, or COASTING.
struct estimator_output track_get_estimate(const struct track *p_track);

// Apply an accepted measurement (m, rad) after prediction and gating.
// Confirm after three hits including the initial measurement; recover COASTING immediately.
// Clear misses; do nothing for EMPTY or LOST.
void track_apply_measurement(
    struct track *p_track,
    float measured_range_m,
    float measured_azimuth_rad
);

// Record a gate rejection after prediction, not a no-reception timeout.
// A TENTATIVE miss clears the track; established tracks coast and become LOST
// after five consecutive misses. Do nothing for EMPTY or LOST.
void track_apply_miss(struct track *p_track);

#endif
