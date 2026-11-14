#ifndef UART_RX_H
#define UART_RX_H

#include <stdint.h>

// Initialize the RX ring and parser, enable UART RX interrupts, and release the RX thread.
// Call once during startup; concurrent reinitialization is not supported.
// Return 0 on success, -ENODEV if the device is not ready, or the negative errno
// returned by callback registration. Initialization failure does not release the RX thread.
int uart_rx_init(void);

typedef enum {
    // Discard all buffered bytes and any incomplete frame.
    RX_OVERFLOW_POLICY_DISCARD_ALL,
    // Process frames that can be completed from buffered bytes, then discard the remainder.
    RX_OVERFLOW_POLICY_DRAIN_BUFFER,
} RxOverflowPolicy;

struct uart_rx_stats {
    // Failed ring pushes that start a new overflow recovery episode.
    uint32_t ring_overflow_events;
    // FIFO bytes dropped by the ISR while full or recovering; excludes hardware loss.
    uint32_t rx_dropped_bytes;
    // Unread ring bytes discarded during recovery; excludes ISR drops and parser-held bytes.
    uint32_t recovery_discarded_bytes;
};

// Copy cumulative counters without resetting them.
// Require a non-NULL output pointer.
// Fields are read independently, not as one simultaneous snapshot.
void uart_rx_get_stats(struct uart_rx_stats *out_stats);

#endif
