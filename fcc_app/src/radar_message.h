#ifndef RADAR_MESSAGE_H
#define RADAR_MESSAGE_H

#include <stdint.h>
#include <stdbool.h>

// [timestamp 4B][range 2B][azimuth 2B]
struct radar_measurement {
    uint32_t timestamp_ms;
    uint16_t range;
    int16_t azimuth;
};

bool decode_radar_measurement(
    const uint8_t *p_payload,
    uint8_t payload_length,
    struct radar_measurement *p_measurement);

bool validate_radar_measurement(
    const struct radar_measurement *p_measurement);

#endif