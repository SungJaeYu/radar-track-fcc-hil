#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include <stdbool.h>
#include <stdint.h>

// One slot is reserved to distinguish full from empty: usable capacity is size - 1.
// uint8_t indices require a buffer size between 2 and 256.
#define RING_BUFFER_SIZE 256

// Single-producer/single-consumer storage with no internal locking.
// Callers must protect shared access; volatile does not provide synchronization.
// In the UART path, the ISR produces bytes and the RX thread consumes them.
struct ring_buffer {
    uint8_t data[RING_BUFFER_SIZE];
    // Next write position.
    volatile uint8_t head;
    // Next read position.
    volatile uint8_t tail;
};

// Require a non-NULL ring. Reset indices and logically discard buffered bytes.
// The data array is not erased; call only when access is exclusive.
void ring_init(struct ring_buffer *p_rb);
// Require a non-NULL ring. Return true if stored, or false if full.
// On full, preserve existing bytes and discard the new byte.
bool ring_push(struct ring_buffer *p_rb, uint8_t c);
// Require non-NULL ring and output pointers. Return true if a byte was read.
// Return false if empty, leaving the output unchanged.
bool ring_pop(struct ring_buffer *p_rb, uint8_t *out_byte);

#endif
