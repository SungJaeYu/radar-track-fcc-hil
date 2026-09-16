#include "gating.h"
#include "angle_utils.h"

#include <math.h>

#define RANGE_GATE_M       100.0f
#define AZIMUTH_GATE_DEG    10.0f

bool gating_check(
    float measured_range,
    float predicted_range,
    float measured_azimuth,
    float predicted_azimuth)
{
    const float range_error =
        measured_range - predicted_range;

    const float azimuth_error =
        angle_diff_deg(
            measured_azimuth,
            predicted_azimuth);

    return
        fabsf(range_error) <= RANGE_GATE_M &&
        fabsf(azimuth_error) <= AZIMUTH_GATE_DEG;
}