#ifndef GATING_H
#define GATING_H

#include <stdbool.h>

// Compare measurement and prediction in meters and radians.
// Require finite inputs and a finite angular subtraction result.
// Return true only if both absolute errors are within the configured half-widths.
// CONFIG_RADAR_RANGE_GATE_M is meters; CONFIG_RADAR_AZIMUTH_GATE_DEG is whole degrees.
// Defaults are +/-100 m and +/-10 degrees. Boundaries are inclusive; angles are wrapped.
// This function does not update filters or track state.
bool gating_check(
    float measured_range_m,
    float predicted_range_m,
    float measured_azimuth_rad,
    float predicted_azimuth_rad);

#endif
