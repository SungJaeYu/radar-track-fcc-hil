#ifndef TEST_TX_ZEPHYR_H
#define TEST_TX_ZEPHYR_H
// Synchronous mocks: no scheduler, IRQ timing, or hardware UART is modeled.
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#define ARG_UNUSED(x) (void)(x)
#define K_FOREVER (-1)
#define K_NO_WAIT 0
#define K_THREAD_DEFINE(...)
struct device {
    int unused;
};
static const struct device test_device;
static bool ready;
#define DT_NODELABEL(x) 0
#define DEVICE_DT_GET(x) (&test_device)
static bool device_is_ready(const struct device *p_dev)
{
    assert(p_dev == &test_device);
    return ready;
}

struct k_sem {
    unsigned count;
};
#define K_SEM_DEFINE(name, initial, limit) struct k_sem name = {initial}

static void k_sem_give(struct k_sem *p_sem)
{
    p_sem->count = 1;
}

static int k_sem_take(struct k_sem *p_sem, int timeout)
{
    assert(timeout == K_FOREVER && p_sem->count == 1);
    p_sem->count = 0;
    return 0;
}

struct k_msgq {
    unsigned char *p_data;
    size_t size, capacity, head, count;
};
#define K_MSGQ_DEFINE(name, size, count, align)                                                    \
    unsigned char name##_data[(size) * (count)];                                                   \
    struct k_msgq name = {name##_data, size, count, 0, 0}

static jmp_buf worker_exit;
static unsigned put_calls;
static int k_msgq_put(struct k_msgq *p_queue, const void *p_data, int timeout)
{
    assert(timeout == K_NO_WAIT);
    put_calls++;
    if (p_queue->count == p_queue->capacity) {
        return -ENOMSG;
    }

    size_t tail = (p_queue->head + p_queue->count) % p_queue->capacity;
    memcpy(p_queue->p_data + tail * p_queue->size, p_data, p_queue->size);
    p_queue->count++;
    return 0;
}

static int k_msgq_get(struct k_msgq *p_queue, void *out_data, int timeout)
{
    assert(timeout == K_FOREVER);
    // End the test when the real worker would block waiting for more requests.
    if (p_queue->count == 0) {
        longjmp(worker_exit, 1);
    }

    memcpy(out_data, p_queue->p_data + p_queue->head * p_queue->size, p_queue->size);
    p_queue->head = (p_queue->head + 1) % p_queue->capacity;
    p_queue->count--;
    return 0;
}

static uint8_t sent[512];
static size_t sent_count;
static void uart_poll_out(const struct device *p_dev, unsigned char byte)
{
    assert(p_dev == &test_device && ready && sent_count < sizeof(sent));
    sent[sent_count++] = byte;
}

static char log_output[256];
static int printk(const char *p_format, ...)
{
    va_list args;
    va_start(args, p_format);
    int count = vsnprintf(log_output, sizeof(log_output), p_format, args);
    va_end(args);
    return count;
}

#endif
