#ifndef TEST_PERF_KERNEL_H
#define TEST_PERF_KERNEL_H

#include <stddef.h>
#include <stdint.h>

// Only the reporter's uptime and stack-query boundary are modeled here.
struct k_thread {
    unsigned int index;
};
typedef struct k_thread *k_tid_t;
int64_t k_uptime_get(void);
int k_thread_stack_space_get(const struct k_thread *p_thread, size_t *out_unused);

#endif
