#include "ring_buffer.h"

void ring_init(struct ring_buffer *rb) {
    rb->head = 0;
    rb->tail = 0;
}

bool ring_push(struct ring_buffer *rb, uint8_t c) {
    if ((rb->head + 1) % BUFFER_SIZE == rb->tail) {
        return false;
    }
    rb->data[rb->head] = c;
    rb->head = (rb->head + 1) % BUFFER_SIZE;
    return true;
}

bool ring_pop(struct ring_buffer *rb, uint8_t *c) {
    if (rb->head == rb->tail) {
        return false;
    }
    *c = rb->data[rb->tail];
    rb->tail = (rb->tail + 1) % BUFFER_SIZE;
    return true;
}