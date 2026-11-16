#ifndef BYTE_ORDER_H
#define BYTE_ORDER_H

#include <stdint.h>
#include <float.h>
#include <string.h>

_Static_assert(sizeof(float) == sizeof(uint32_t), "Float serialization requires 32-bit float storage");
_Static_assert(
    FLT_RADIX == 2 && FLT_MANT_DIG == 24 && FLT_MIN_EXP == -125 && FLT_MAX_EXP == 128,
    "Float serialization requires binary32 characteristics"
);

// Write one unsigned integer in big-endian order.
// Require at least four writable bytes; no alignment is required.
static inline void write_u32_be(uint32_t value, uint8_t *out_bytes)
{
    out_bytes[0] = (uint8_t)(value >> 24);
    out_bytes[1] = (uint8_t)(value >> 16);
    out_bytes[2] = (uint8_t)(value >> 8);
    out_bytes[3] = (uint8_t)value;
}

// Preserve the float bit pattern and write it in big-endian order.
// Require four writable bytes and IEEE 754 binary32 float representation.
static inline void write_f32_be(float value, uint8_t *out_bytes)
{
    uint32_t bits;

    memcpy(&bits, &value, sizeof(bits));
    write_u32_be(bits, out_bytes);
}

#endif
