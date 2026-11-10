#include "common/ring_buffer.h"

void ring_init(struct ring_buffer *p_rb)
{
    p_rb->head = 0;
    p_rb->tail = 0;
}

bool ring_push(struct ring_buffer *p_rb, uint8_t c)
{
    // Reserving one slot prevents a full ring from appearing empty (head == tail).
    if ((p_rb->head + 1) % RING_BUFFER_SIZE == p_rb->tail) {
        return false;
    }
    p_rb->data[p_rb->head] = c;
    p_rb->head = (p_rb->head + 1) % RING_BUFFER_SIZE;
    return true;
}

bool ring_pop(struct ring_buffer *p_rb, uint8_t *out_byte)
{
    if (p_rb->head == p_rb->tail) {
        return false;
    }
    *out_byte = p_rb->data[p_rb->tail];
    p_rb->tail = (p_rb->tail + 1) % RING_BUFFER_SIZE;
    return true;
}
