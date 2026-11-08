#ifndef CRC_16_H
#define CRC_16_H

#include <stdint.h>

#define INIT_CRC 0xFFFF

uint16_t crc16_update(uint16_t crc, const uint8_t byte);

#endif