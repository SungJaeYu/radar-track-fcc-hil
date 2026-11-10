#ifndef CRC_16_H
#define CRC_16_H

#include <stdint.h>

// CRC-16/IBM-3740: polynomial 0x1021, initial value 0xFFFF,
// no input/output reflection, and no final XOR.
#define CRC16_INITIAL_VALUE 0xFFFF

// Update a running CRC with one byte and return the new CRC.
// Start with CRC16_INITIAL_VALUE and pass each returned value into the next update.
// This algorithm does not define frame coverage or wire byte order.
uint16_t crc16_update(uint16_t crc, const uint8_t byte);

#endif
