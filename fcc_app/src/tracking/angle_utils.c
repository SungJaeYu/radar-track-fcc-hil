#include "tracking/angle_utils.h"

#define PI_F 3.14159265358979323846f

float wrap_angle_rad(float angle_rad)
{
    while (angle_rad > PI_F) {
        angle_rad -= 2.0f * PI_F;
    }

    while (angle_rad <= -PI_F) {
        angle_rad += 2.0f * PI_F;
    }

    return angle_rad;
}

float angle_diff_rad(float a_rad, float b_rad)
{
    return wrap_angle_rad(a_rad - b_rad);
}
