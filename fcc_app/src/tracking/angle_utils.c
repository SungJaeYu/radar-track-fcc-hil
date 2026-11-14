#include "tracking/angle_utils.h"

#include <math.h>

#include <zephyr/sys/__assert.h>

#include "common/math_constants.h"

float wrap_angle_rad(float angle_rad)
{
    __ASSERT(isfinite(angle_rad), "Angle value must be finite");

    // Remove complete turns without a subtraction loop that scales with input magnitude.
    // fmodf preserves the dividend's sign and leaves a remainder in (-2*pi, 2*pi).
    float wrapped = fmodf(angle_rad, 2.0f * MATH_PI_F);

    // Preserve the wire-independent internal convention (-pi, pi], including -pi -> +pi.
    if (wrapped > MATH_PI_F) {
        wrapped -= 2.0f * MATH_PI_F;
    } else if (wrapped <= -MATH_PI_F) {
        wrapped += 2.0f * MATH_PI_F;
    }

    return wrapped;
}

float angle_diff_rad(float a_rad, float b_rad)
{
    return wrap_angle_rad(a_rad - b_rad);
}
