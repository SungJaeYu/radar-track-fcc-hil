#ifndef UART_RX_H
#define UART_RX_H

int uart_rx_init(void);

enum rx_overflow_policy {
    // Discard all buffered bytes and any incomplete frame.
    RX_OVERFLOW_DISCARD_ALL,
    // Process frames that can be completed from buffered bytes, then discard the remainder.
    RX_OVERFLOW_DRAIN_BUFFER,
};

// Build-time policy: select RX_OVERFLOW_DRAIN_BUFFER to test drain recovery.
#define RX_OVERFLOW_POLICY RX_OVERFLOW_DRAIN_BUFFER

#endif
