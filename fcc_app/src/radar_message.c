#include "radar_message.h"

bool decode_radar_measurement(
    const uint8_t *p_payload, 
    uint8_t payload_length,
    struct radar_measurement *p_measurement)
{
    if (payload_length != 8) {
        return false;
    }

    p_measurement->timestamp_ms = 
        (uint32_t)p_payload[0] << 24 | 
        (uint32_t)p_payload[1] << 16 | 
        (uint32_t)p_payload[2] << 8 | 
        (uint32_t)p_payload[3];

    p_measurement->range = 
        (uint16_t)p_payload[4] << 8 | 
        (uint16_t)p_payload[5];

    p_measurement->azimuth = 
        (int16_t)(
            p_payload[6] << 8 | 
            p_payload[7]
        );
    
    return true;
}

bool validate_radar_measurement(
    const struct radar_measurement *p_measurement)
{
    if (p_measurement->range == 0) {
        return false;
    }

    if (p_measurement->azimuth < -180 ||
        p_measurement->azimuth > 180) {
        return false;
    }

    return true;
}