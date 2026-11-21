#include "diagnostics/perf_monitor.h"

#include <errno.h>
#include <stddef.h>

#include <zephyr/spinlock.h>

// One short lock protects updates and snapshots across RX, handler, TX, and main.
// No clocks, stack scans, queue operations, or logging run while this lock is held.
static struct k_spinlock stats_lock;
static struct perf_stats stats;

// Only the handler starts/ends a context. The lock also makes snapshots coherent.
static struct {
    bool active;
    bool submitted;
    bool valid_time;
    int64_t received_uptime_ms;
    int64_t handler_start_uptime_ms;
} measurement;

static void increment(uint32_t *p_value)
{
    if (*p_value != UINT32_MAX) {
        ++*p_value;
    }
}

static void record_latency(struct perf_latency_stats *p_latency, int64_t elapsed_ms)
{
    // Stop the whole aggregate together so histogram counts still match count.
    if (p_latency->sample_count == UINT32_MAX) {
        return;
    }
    const uint32_t sample_elapsed_ms = elapsed_ms > UINT32_MAX ? UINT32_MAX : (uint32_t)elapsed_ms;
    ++p_latency->sample_count;
    // Each sample is clamped to uint32, so count * sample fits in uint64.
    p_latency->total_ms += sample_elapsed_ms;
    if (sample_elapsed_ms > p_latency->max_ms) {
        p_latency->max_ms = sample_elapsed_ms;
    }
    const uint32_t bin = sample_elapsed_ms > PERF_LATENCY_BUDGET_MS ? PERF_LATENCY_BUDGET_MS + 1U : sample_elapsed_ms;
    ++p_latency->histogram_counts[bin];
    if (sample_elapsed_ms > PERF_LATENCY_BUDGET_MS) {
        ++p_latency->over_budget_count;
    }
}

void perf_monitor_begin_measurement(int64_t received_uptime_ms, int64_t handler_start_uptime_ms)
{
    const k_spinlock_key_t key = k_spin_lock(&stats_lock);
    measurement.active = true;
    measurement.submitted = false;
    measurement.valid_time = received_uptime_ms >= 0 && handler_start_uptime_ms >= received_uptime_ms;
    measurement.received_uptime_ms = received_uptime_ms;
    measurement.handler_start_uptime_ms = handler_start_uptime_ms;
    increment(&stats.measurements);
    if (measurement.valid_time) {
        record_latency(&stats.queue_wait, handler_start_uptime_ms - received_uptime_ms);
    } else {
        increment(&stats.invalid_timestamps);
    }
    k_spin_unlock(&stats_lock, key);
}

void perf_monitor_record_track_submit(int64_t submit_uptime_ms, int result)
{
    const k_spinlock_key_t key = k_spin_lock(&stats_lock);
    // Ignore controls, late calls, and duplicate completions of the same input.
    if (measurement.active && !measurement.submitted) {
        measurement.submitted = true;
        if (result != 0) {
            increment(&stats.track_submit_failed);
        } else if (measurement.valid_time) {
            if (submit_uptime_ms >= measurement.handler_start_uptime_ms) {
                record_latency(&stats.processing, submit_uptime_ms - measurement.handler_start_uptime_ms);
                record_latency(&stats.rx_to_submit, submit_uptime_ms - measurement.received_uptime_ms);
            } else {
                increment(&stats.invalid_timestamps);
            }
        }
    }
    k_spin_unlock(&stats_lock, key);
}

void perf_monitor_end_measurement(void)
{
    const k_spinlock_key_t key = k_spin_lock(&stats_lock);
    if (measurement.active && !measurement.submitted) {
        increment(&stats.without_submit);
    }
    measurement.active = false;
    k_spin_unlock(&stats_lock, key);
}

void perf_monitor_record_queue(PerfQueue queue, int result, uint32_t used)
{
    if ((unsigned int)queue >= PERF_QUEUE_COUNT) {
        return;
    }
    const k_spinlock_key_t key = k_spin_lock(&stats_lock);
    struct perf_queue_stats *p_queue = &stats.queues[queue];
    increment(result == 0 ? &p_queue->accepted_count : &p_queue->rejected_count);
    if (used > p_queue->max_observed_depth) {
        p_queue->max_observed_depth = used;
    }
    k_spin_unlock(&stats_lock, key);
}

void perf_monitor_record_tx(bool completed)
{
    const k_spinlock_key_t key = k_spin_lock(&stats_lock);
    increment(completed ? &stats.tx_poll_completed : &stats.tx_encode_failed);
    k_spin_unlock(&stats_lock, key);
}

int perf_monitor_get_stats(struct perf_stats *out_stats)
{
    if (out_stats == NULL) {
        return -EINVAL;
    }
    const k_spinlock_key_t key = k_spin_lock(&stats_lock);
    *out_stats = stats;
    k_spin_unlock(&stats_lock, key);
    return 0;
}
