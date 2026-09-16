#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include <stdbool.h>
#include <stdint.h>

#define BUFFER_SIZE 8

struct ring_buffer {
    uint8_t data[BUFFER_SIZE];
    volatile uint8_t head;
    volatile uint8_t tail;
};

void ring_init(struct ring_buffer *rb);
bool ring_push(struct ring_buffer *rb, uint8_t c);
bool ring_pop(struct ring_buffer *rb, uint8_t *c);

#endif