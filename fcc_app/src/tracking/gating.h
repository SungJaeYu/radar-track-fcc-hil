#ifndef GATING_H
#define GATING_H

#include <stdbool.h>

// Compare measurement and prediction in meters and radians.
// Return true only if both errors are within the fixed gates, including boundaries.
// The current gates are 100 m and 10 degrees; angular error is wrapped.
// This function does not update filters or track state.
bool gating_check(
    float measured_range_m,
    float predicted_range_m,
    float measured_azimuth_rad,
    float predicted_azimuth_rad);

#endif
