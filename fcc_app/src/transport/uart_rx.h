#ifndef UART_RX_H
#define UART_RX_H

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

#endif
