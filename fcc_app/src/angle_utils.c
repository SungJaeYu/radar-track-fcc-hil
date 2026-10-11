#include "angle_utils.h"

#define PI_F 3.14159265358979323846f

float wrap_angle_rad(float angle)
{
    while (angle > PI_F) {
        angle -= 2 * PI_F;
    }

    while (angle <= -PI_F) {
        angle += 2 * PI_F;
    }

    return angle;
}

float angle_diff_rad(float a, float b)
{
    return wrap_angle_rad(a - b);
}
