#ifndef GATING_H
#define GATING_H

#include <stdbool.h>

bool gating_check(
    float measured_range,
    float predicted_range,
    float measured_azimuth,
    float predicted_azimuth);

#endif