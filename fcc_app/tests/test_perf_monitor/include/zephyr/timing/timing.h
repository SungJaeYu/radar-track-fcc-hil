#ifndef TEST_TIMING_H
#define TEST_TIMING_H

#include <stdint.h>

// The host fixture uses a synthetic 100 MHz timer, not the board clock.
static inline uint64_t timing_cycles_to_ns(uint64_t cycles)
{
    return cycles * UINT64_C(10);
}

#endif
