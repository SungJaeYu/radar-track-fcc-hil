#ifndef ESTIMATOR_H
#define ESTIMATOR_H

#include "tracking/kalman_cv.h"

// Independent Kalman filter states for range and azimuth.
struct estimator {
    struct kalman_cv_filter range_filter;
    struct kalman_cv_filter azimuth_filter;
};

// Common estimate returned to callers.
struct estimator_output {
    float range_m;             // Range [m]
    float azimuth_rad;         // Azimuth [rad]
    float range_rate_mps;       // Range rate [m/s]
    float azimuth_rate_radps;   // Azimuth rate [rad/s]
};

// All APIs require valid pointers to caller-owned objects.
// Callers validate finite measurements; track lifecycle management is outside this API.

// Initialize the internal state from the first measurement.
// Call before predict, correct, or get_output.
void estimator_init(
    struct estimator *p_estimator,
    float initial_range_m,
    float initial_azimuth_rad
);

// Predict the state forward by interval_s.
// The caller validates timestamps and computes a finite, positive interval_s [s].
void estimator_predict(struct estimator *p_estimator, float interval_s);

// Correct the predicted state using a measurement accepted by the gate.
void estimator_correct(
    struct estimator *p_estimator,
    float measured_range_m,
    float measured_azimuth_rad
);

// Return the initialized estimate in common units without modifying internal state.
// The caller determines estimate validity from the track lifecycle state.
struct estimator_output estimator_get_output(const struct estimator *p_estimator);

#endif
