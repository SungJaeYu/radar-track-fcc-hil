#ifndef ANGLE_UTILS_H
#define ANGLE_UTILS_H

// Normalize a practical, finite angle in radians into (-pi, pi].
// Uses repeated subtraction/addition; not intended for arbitrarily large inputs.
float wrap_angle_rad(float angle);

// Return the normalized signed difference a - b in radians, in (-pi, pi].
// Inputs must satisfy the same assumptions as wrap_angle_rad().
float angle_diff_rad(float a, float b);


#endif