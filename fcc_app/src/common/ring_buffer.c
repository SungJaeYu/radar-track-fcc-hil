#include "common/ring_buffer.h"

#include <stddef.h>

#include <zephyr/sys/__assert.h>

_Static_assert(RING_BUFFER_SIZE >= 2, "Ring buffer needs at least two slots");
_Static_assert(RING_BUFFER_SIZE <= UINT8_MAX + 1U, "Ring buffer exceeds uint8_t index range");

void ring_init(struct ring_buffer *p_rb)
{
    __ASSERT(p_rb != NULL, "Ring buffer pointer is NULL");

    p_rb->head = 0;
    p_rb->tail = 0;
}

bool ring_push(struct ring_buffer *p_rb, uint8_t c)
{
    __ASSERT(p_rb != NULL, "Ring buffer pointer is NULL");

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
    __ASSERT(p_rb != NULL, "Ring buffer pointer is NULL");
    __ASSERT(out_byte != NULL, "Output byte pointer is NULL");

    if (p_rb->head == p_rb->tail) {
        return false;
    }
    *out_byte = p_rb->data[p_rb->tail];
    p_rb->tail = (p_rb->tail + 1) % RING_BUFFER_SIZE;
    return true;
}
