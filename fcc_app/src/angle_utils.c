#include "angle_utils.h"

float wrap_angle_deg(float angle)
{
    while (angle > 180.0f) {
        angle -= 360.0f;
    }

    while (angle <= -180.0f) {
        angle += 360.0f;
    }

    return angle;
}

float angle_diff_deg(float a, float b)
{
    return wrap_angle_deg(a - b);
}
