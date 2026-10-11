#ifndef TRACK_H
#define TRACK_H

#include <stdint.h>
#include <stdbool.h>

#include "kalman_cv.h"

typedef enum {
    TRACK_EMPTY,
    TRACK_TENTATIVE,
    TRACK_CONFIRMED,
    TRACK_COASTING,
    TRACK_LOST
} TrackState;

struct track {
    TrackState state;

    struct kalman_cv_filter range_filter;
    struct kalman_cv_filter azimuth_filter;

    uint32_t hit_count;
    uint32_t miss_count;

    uint32_t last_measurement_time_ms;
};

void track_init(
    struct track *p_track
);

void track_start(
    struct track *p_track,
    float initial_range,
    float initial_azimuth_rad,
    uint32_t timestamp_ms
);

bool track_predict(
    struct track *p_track,
    uint32_t timestamp_ms
);

void track_hit(
    struct track *p_track,
    float measured_range,
    float measured_azimuth_rad
);

void track_miss(
    struct track *p_track
);

#endif