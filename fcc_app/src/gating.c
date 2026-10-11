#include "gating.h"
#include "angle_utils.h"

#include <math.h>

#define PI_F 3.14159265358979323846f

#define RANGE_GATE_M       100.0f
#define AZIMUTH_GATE_RAD    (10.0f * PI_F / 180.0f)

bool gating_check(
    float measured_range,
    float predicted_range,
    float measured_azimuth,
    float predicted_azimuth)
{
    const float range_error =
        measured_range - predicted_range;

    const float azimuth_error =
        angle_diff_rad(
            measured_azimuth,
            predicted_azimuth);

    return
        fabsf(range_error) <= RANGE_GATE_M &&
        fabsf(azimuth_error) <= AZIMUTH_GATE_RAD;
}