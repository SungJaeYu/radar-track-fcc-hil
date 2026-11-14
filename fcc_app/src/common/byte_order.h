#ifndef BYTE_ORDER_H
#define BYTE_ORDER_H

#include <stdint.h>

// Write one unsigned integer in big-endian order.
// Require at least four writable bytes; no alignment is required.
static inline void write_u32_be(uint32_t value, uint8_t *out_bytes)
{
    out_bytes[0] = (uint8_t)(value >> 24);
    out_bytes[1] = (uint8_t)(value >> 16);
    out_bytes[2] = (uint8_t)(value >> 8);
    out_bytes[3] = (uint8_t)value;
}

#endif
