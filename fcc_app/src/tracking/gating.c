#include "tracking/gating.h"

#include <math.h>

#include "common/math_constants.h"
#include "tracking/angle_utils.h"

// Kconfig uses integers for tuning; convert once into the filter's meters/radians.
#define RANGE_GATE_M ((float)CONFIG_RADAR_RANGE_GATE_M)
#define AZIMUTH_GATE_RAD ((float)CONFIG_RADAR_AZIMUTH_GATE_DEG * MATH_PI_F / 180.0f)

bool gating_check(
    float measured_range_m,
    float predicted_range_m,
    float measured_azimuth_rad,
    float predicted_azimuth_rad)
{
    // Compare against the predicted state at the measurement timestamp.
    const float range_error_m =
        measured_range_m - predicted_range_m;

    // Use the shortest signed angular difference across the -pi/+pi boundary.
    const float azimuth_error_rad =
        angle_diff_rad(
            measured_azimuth_rad,
            predicted_azimuth_rad);

    // Both independent inclusive thresholds must pass (a rectangular gate).
    // Gate rejection is a tracking decision; it is not a malformed UART frame.
    return
        fabsf(range_error_m) <= RANGE_GATE_M &&
        fabsf(azimuth_error_rad) <= AZIMUTH_GATE_RAD;
}
