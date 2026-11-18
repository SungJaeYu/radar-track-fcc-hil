#ifndef TEST_PERF_SPINLOCK_H
#define TEST_PERF_SPINLOCK_H

#include <assert.h>
#include <pthread.h>

// A host mutex checks concurrent updates/snapshots, not Zephyr IRQ latency.
struct k_spinlock {
    int unused;
};
typedef int k_spinlock_key_t;
static pthread_mutex_t test_perf_mutex = PTHREAD_MUTEX_INITIALIZER;

static inline k_spinlock_key_t k_spin_lock(struct k_spinlock *p_lock)
{
    (void)p_lock;
    assert(pthread_mutex_lock(&test_perf_mutex) == 0);
    return 0;
}

static inline void k_spin_unlock(struct k_spinlock *p_lock, k_spinlock_key_t key)
{
    (void)p_lock;
    (void)key;
    assert(pthread_mutex_unlock(&test_perf_mutex) == 0);
}

#endif
