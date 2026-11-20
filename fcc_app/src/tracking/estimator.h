#ifndef TRACKING_ESTIMATOR_H
#define TRACKING_ESTIMATOR_H

// The build selects the backend's storage layout for every consumer together.
// Callers own storage but must not access its backend-specific fields.
#ifndef RADAR_ESTIMATOR_HEADER
#define RADAR_ESTIMATOR_HEADER "tracking/estimators/polar_cv.h"
#endif
#include RADAR_ESTIMATOR_HEADER

// Common observation-space output, independent of a backend's internal state.
struct estimator_output {
    float range_m;
    float azimuth_rad;
    float range_rate_mps;
    float azimuth_rate_radps;
};

// Non-NULL, caller-owned objects; init before predict/correct/get_output.
// Inputs are validated by the caller: finite m/rad, finite nonnegative dt in s.
// No heap or shared mutable state. Each track owns an independent estimator.
const char *estimator_name(void);
void estimator_init(struct estimator *p_estimator, float range_m, float azimuth_rad);
void estimator_predict(struct estimator *p_estimator, float dt_s);
void estimator_correct(struct estimator *p_estimator, float range_m, float azimuth_rad);
// Lifecycle validity belongs to track, not this snapshot. Never exposes covariance.
struct estimator_output estimator_get_output(const struct estimator *p_estimator);

#endif
