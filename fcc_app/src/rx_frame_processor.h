#ifndef RX_FRAME_PROCESSOR_H
#define RX_FRAME_PROCESSOR_H

#include <stdint.h>

// Reset partial frame assembly without clearing diagnostic counters.
void rx_frame_processor_reset(void);

// Feed one byte and submit a frame only after successful validation.
void rx_frame_processor_feed(uint8_t byte);

#endif