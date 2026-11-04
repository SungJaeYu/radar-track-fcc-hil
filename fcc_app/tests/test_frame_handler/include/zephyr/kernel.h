#ifndef TEST_HANDLER_KERNEL_H
#define TEST_HANDLER_KERNEL_H
// Only synchronous handler behavior is tested; no RTOS scheduling is simulated.
#include <stddef.h>
#include <stdint.h>
#include <errno.h>
#define ARG_UNUSED(x) (void)(x)
#define K_THREAD_DEFINE(...)
#define K_MSGQ_DEFINE(name, size, count, align) int name
#define K_MSEC(x) (x)
#define K_NO_WAIT 0
static int64_t k_uptime_get(void) { return 0; }
static int k_msgq_get(int *p_queue, void *p_data, int timeout)
{
    (void)p_queue; (void)p_data; (void)timeout;
    return -ENOMSG;
}
static int k_msgq_put(int *p_queue, const void *p_data, int timeout)
{
    (void)p_queue; (void)p_data; (void)timeout;
    return -ENOMSG;
}
#endif
