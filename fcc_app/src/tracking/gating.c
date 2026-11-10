#include "tracking/gating.h"

#include <math.h>

#include "tracking/angle_utils.h"

#define PI_F 3.14159265358979323846f

#define RANGE_GATE_M       100.0f
#define AZIMUTH_GATE_RAD    (10.0f * PI_F / 180.0f)

bool gating_check(
    float measured_range_m,
    float predicted_range_m,
    float measured_azimuth_rad,
    float predicted_azimuth_rad)
{
    const float range_error_m =
        measured_range_m - predicted_range_m;

    const float azimuth_error_rad =
        angle_diff_rad(
            measured_azimuth_rad,
            predicted_azimuth_rad);

    return
        fabsf(range_error_m) <= RANGE_GATE_M &&
        fabsf(azimuth_error_rad) <= AZIMUTH_GATE_RAD;
}
