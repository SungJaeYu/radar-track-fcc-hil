#ifndef RADAR_MESSAGE_H
#define RADAR_MESSAGE_H

#include <stdint.h>
#include <stdbool.h>

// Wire payload: big-endian uint32 timestamp (ms), uint16 range (m),
// and int32 azimuth (semicircle), totaling 10 bytes.
// This decoded representation uses float meters and radians, not the wire layout.
struct radar_measurement {
    // Sender measurement time, not board uptime.
    uint32_t timestamp_ms;
    float range_m;
    float azimuth_rad;
};

// Decode exactly 10 payload bytes and convert semicircle azimuth to radians.
// Return false for NULL input/output or an incorrect length; leave output unchanged.
// Successful decoding does not imply measurement validity or timestamp acceptance.
bool decode_radar_measurement(
    const uint8_t *p_payload,
    uint8_t payload_length,
    struct radar_measurement *out_measurement);

// Require a non-NULL decoded measurement. Return false for zero range or
// azimuth outside [-pi, pi]. Does not check timestamp order, gating, or finiteness.
bool validate_radar_measurement(
    const struct radar_measurement *p_measurement);

#endif
