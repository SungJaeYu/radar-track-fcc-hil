#ifndef TEST_ZEPHYR_H
#define TEST_ZEPHYR_H
// Synchronous test doubles only: no real IRQ scheduling or atomic memory model.
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
struct device { int unused; };
static const struct device test_device;
#define DT_NODELABEL(x) 0
#define DEVICE_DT_GET(x) (&test_device)
#define ARG_UNUSED(x) (void)(x)
#define K_FOREVER (-1)
#define K_MSEC(x) (x)
#define K_THREAD_DEFINE(...)
struct k_sem { unsigned int count; };
#define K_SEM_DEFINE(name, initial, limit) struct k_sem name = { initial }
static void k_sem_give(struct k_sem *sem) { sem->count = 1; }
static int k_sem_take(struct k_sem *sem, int timeout) { (void)timeout; sem->count = 0; return 0; }
static unsigned int irq_locked;
static unsigned int irq_lock(void) { unsigned int old = irq_locked; irq_locked = 1; return old; }
static void irq_unlock(unsigned int key) { irq_locked = key; }
static int64_t k_uptime_get(void) { return 100; }
typedef int atomic_t;
#define ATOMIC_INIT(x) (x)
static int atomic_get(const atomic_t *p) { return *p; }
static void atomic_set(atomic_t *p, int value) { *p = value; }
static void atomic_clear(atomic_t *p) { assert(irq_locked); *p = 0; }
static int printk(const char *format, ...) { (void)format; assert(!irq_locked); return 0; }
static bool device_is_ready(const struct device *dev) { (void)dev; return true; }
static int uart_irq_callback_user_data_set(const struct device *dev,
    void (*callback)(const struct device *, void *), void *data)
{ (void)dev; (void)callback; (void)data; return 0; }
static void uart_irq_rx_enable(const struct device *dev) { (void)dev; }
static void uart_irq_update(const struct device *dev) { (void)dev; }
static const uint8_t *fifo_bytes;
static size_t fifo_size, fifo_position;
static bool uart_irq_is_pending(const struct device *dev)
{ (void)dev; return fifo_position < fifo_size; }
static bool uart_irq_rx_ready(const struct device *dev) { return uart_irq_is_pending(dev); }
static int uart_fifo_read(const struct device *dev, uint8_t *byte, int count)
{
    (void)dev; assert(count == 1);
    if (fifo_position == fifo_size) return 0;
    *byte = fifo_bytes[fifo_position++];
    return 1;
}
#endif
