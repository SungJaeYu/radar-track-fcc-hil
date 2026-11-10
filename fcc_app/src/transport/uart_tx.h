#ifndef UART_TX_H
#define UART_TX_H

#include "protocol/protocol_frame.h"

// Queue-owned copy of a frame to encode and transmit; no caller buffer is retained.
struct send_frame {
    struct protocol_frame frame;
};

// Check the UART device and release the TX thread; UART configuration is provided elsewhere.
// Repeated calls only signal readiness; they do not clear pending requests.
// Call during startup. Return 0 on success or -ENODEV if the device is not ready.
int uart_tx_init(void);

// Copy a request into the eight-entry TX queue without waiting.
// Return 0 when queued, -EINVAL for NULL or an invalid payload length,
// or -ENOMSG when the queue is full. Success does not mean transmission completed.
// Submission does not check device readiness; queued requests wait for successful TX init.
// The caller may reuse the request after this call returns.
int uart_tx_submit(const struct send_frame *p_frame);

#endif
