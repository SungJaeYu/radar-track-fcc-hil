#include "tracking/angle_utils.h"

#include <math.h>

#include <zephyr/sys/__assert.h>

#define PI_F 3.14159265358979323846f

float wrap_angle_rad(float angle_rad)
{
    __ASSERT(isfinite(angle_rad), "Angle value must be finite");

    // Remove complete turns without a subtraction loop that scales with input magnitude.
    // fmodf preserves the dividend's sign and leaves a remainder in (-2*pi, 2*pi).
    float wrapped = fmodf(angle_rad, 2.0f * PI_F);

    // Preserve the wire-independent internal convention (-pi, pi], including -pi -> +pi.
    if (wrapped > PI_F) {
        wrapped -= 2.0f * PI_F;
    } else if (wrapped <= -PI_F) {
        wrapped += 2.0f * PI_F;
    }

    return wrapped;
}

float angle_diff_rad(float a_rad, float b_rad)
{
    return wrap_angle_rad(a_rad - b_rad);
}
