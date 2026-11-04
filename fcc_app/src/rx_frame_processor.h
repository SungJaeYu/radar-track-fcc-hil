#ifndef RX_FRAME_PROCESSOR_H
#define RX_FRAME_PROCESSOR_H

#include <stdint.h>

// Reset partial frame assembly without clearing diagnostic counters.
void rx_frame_processor_reset(void);

// Feed one byte and submit a frame only after successful validation.
void rx_frame_processor_feed(uint8_t byte);

// Discard an incomplete frame after the inactivity timeout.
void rx_frame_processor_check_timeout(int64_t now_ms);


struct rx_frame_stats {
    uint32_t crc_errors;
    uint32_t length_errors;
    uint32_t frame_timeouts;
    uint32_t queue_drops;
};

// Copy cumulative counters; fields are read independently.
void rx_frame_processor_get_stats(struct rx_frame_stats *p_stats);

#endif