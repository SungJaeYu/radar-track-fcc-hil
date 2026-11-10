#include "common/crc16.h"

uint16_t crc16_update(uint16_t crc, const uint8_t byte)
{
    crc ^= (uint16_t)byte << 8;

    for (int i = 0; i < 8; ++i) {
        if (crc & 0x8000) {
            crc = (uint16_t)((crc << 1) ^ 0x1021);
        } else {
            crc = (uint16_t)(crc << 1);
        }
    }

    return crc;
}
