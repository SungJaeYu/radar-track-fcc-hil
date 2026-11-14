#ifndef ANGLE_UTILS_H
#define ANGLE_UTILS_H

// Normalize a finite angle in radians into (-pi, pi] using fmodf.
// Large inputs retain only their existing float precision; lost precision is not recovered.
// Nonfinite inputs violate the contract and are asserted in debug builds.
float wrap_angle_rad(float angle_rad);

// Return the normalized signed difference a_rad - b_rad in radians, in (-pi, pi].
// Both inputs and their subtraction result must be finite.
float angle_diff_rad(float a_rad, float b_rad);

#endif
