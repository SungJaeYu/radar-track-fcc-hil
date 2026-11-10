#ifndef TRACK_H
#define TRACK_H

#include <stdint.h>
#include <stdbool.h>

#include "tracking/kalman_cv.h"

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

    struct kalman_cv_filter range_filter;
    struct kalman_cv_filter azimuth_filter;

    uint32_t hit_count;
    uint32_t miss_count;

    // Sender timestamp of the current filter state, including prediction-only updates.
    uint32_t last_measurement_time_ms;
};

// All track APIs require a non-NULL object owned by the calling processing context.
// Clear state, counters, and timestamp; filter contents remain until track_start().
void track_init(
    struct track *p_track
);

// Initialize filters from a measurement (m, rad, sender ms), enter TENTATIVE,
// and count this initial measurement as the first hit.
void track_start(
    struct track *p_track,
    float initial_range_m,
    float initial_azimuth_rad,
    uint32_t timestamp_ms
);

// Return true after predicting to the supplied sender timestamp.
// Return false for EMPTY, LOST, duplicate/backward time, or a forward gap > 1000 ms.
// LOST and excessive forward gaps reset the track to EMPTY; other failures preserve it.
// Normal uint32 timestamp wraparound is supported. No new track is started here.
bool track_predict(
    struct track *p_track,
    uint32_t timestamp_ms
);

// Apply an accepted measurement (m, rad) after prediction and gating.
// Confirm after three hits including the initial measurement; recover COASTING immediately.
// Clear misses; do nothing for EMPTY or LOST.
void track_hit(
    struct track *p_track,
    float measured_range_m,
    float measured_azimuth_rad
);

// Record a gate rejection after prediction, not a no-reception timeout.
// A TENTATIVE miss clears the track; established tracks coast and become LOST
// after five consecutive misses. Do nothing for EMPTY or LOST.
void track_miss(
    struct track *p_track
);

#endif
