#ifndef RX_FRAME_PROCESSOR_H
#define RX_FRAME_PROCESSOR_H

#include <stdint.h>

// reset/feed/check_timeout mutate shared parser state: call from the RX owner context
// after startup initialization, not concurrently from other threads or the UART ISR.
// Reset partial frame assembly without clearing diagnostic counters.
void rx_frame_processor_reset(void);

// Feed one byte; submit only after frame length/CRC validation.
// Measurement semantics are validated later by the handler. A full queue drops the frame.
void rx_frame_processor_feed(uint8_t byte);

// Use board uptime in ms. Discard a partial frame after >= 1000 ms since the last
// byte was processed here (not its hardware arrival). Idle PARSER_STATE_WAIT_STX is not monitored.
void rx_frame_processor_check_timeout(int64_t now_ms);


struct rx_frame_stats {
    uint32_t crc_errors;
    uint32_t length_errors;
    uint32_t frame_timeouts;
    // Complete frames rejected by the handler queue, not ring overflow bytes.
    uint32_t queue_drops;
};

// Require a non-NULL output. Copy cumulative counters without resetting them.
// May be read from another thread; each atomic field is read independently,
// so the result is not a simultaneous snapshot of all counters.
void rx_frame_processor_get_stats(struct rx_frame_stats *p_stats);

#endif