#include "radar_message.h"
#include <stddef.h>

#define PI_F 3.14159265358979323846f
#define SEMICIRCLE_SCALE 2147483648.0f

bool decode_radar_measurement(
    const uint8_t *p_payload,
    uint8_t payload_length,
    struct radar_measurement *p_measurement)
{
    if (p_payload == NULL || p_measurement == NULL) {
        return false;
    }

    if (payload_length != 10U) {
        return false;
    }

    const uint32_t timestamp_ms =
        ((uint32_t)p_payload[0] << 24) |
        ((uint32_t)p_payload[1] << 16) |
        ((uint32_t)p_payload[2] << 8)  |
        (uint32_t)p_payload[3];

    const uint16_t range_m =
        ((uint16_t)p_payload[4] << 8) |
        (uint16_t)p_payload[5];

    const int32_t azimuth_sc =
        (int32_t)(
            ((uint32_t)p_payload[6] << 24) |
            ((uint32_t)p_payload[7] << 16) |
            ((uint32_t)p_payload[8] << 8)  |
            (uint32_t)p_payload[9]
        );

    p_measurement->timestamp_ms = timestamp_ms;
    p_measurement->range_m = (float)range_m;

    p_measurement->azimuth_rad =
        (float)azimuth_sc *
        (PI_F / SEMICIRCLE_SCALE);

    return true;
}

bool validate_radar_measurement(
    const struct radar_measurement *p_measurement)
{
    if (p_measurement->range_m == 0) {
        return false;
    }

    if (p_measurement->azimuth_rad < -PI_F ||
        p_measurement->azimuth_rad > PI_F) {
        return false;
    }

    return true;
}
