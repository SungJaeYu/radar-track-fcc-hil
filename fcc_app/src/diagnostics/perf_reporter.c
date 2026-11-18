#include "diagnostics/perf_monitor.h"

#include <stddef.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

// Bench-only summaries have a fixed INFO level, independent of per-frame DEBUG logs.
LOG_MODULE_REGISTER(radar_perf, LOG_LEVEL_INF);

extern const k_tid_t uart_rx_tid;
extern const k_tid_t frame_handler_tid;
extern const k_tid_t uart_tx_tid;

// Main is the sole caller. Keep the large snapshot off its small stack.
static struct perf_stats snapshot;
static size_t min_unused[3] = {SIZE_MAX, SIZE_MAX, SIZE_MAX};

static uint32_t percentile_bin(const struct perf_timing_stats *p_timing, uint32_t percentile)
{
    if (p_timing->count == 0) {
        return 0;
    }
    const uint64_t rank = ((uint64_t)p_timing->count * percentile + 99U) / 100U;
    uint64_t cumulative = 0;
    for (uint32_t i = 0; i < PERF_LATENCY_BIN_COUNT; ++i) {
        cumulative += p_timing->bins[i];
        if (cumulative >= rank) {
            return i;
        }
    }
    return PERF_LATENCY_BUDGET_MS + 1U;
}

static void log_timing(const char *p_name, const struct perf_timing_stats *p_timing)
{
    // Percentile bin 21 means >20 ms, not an exact 21 ms observation.
    LOG_INF("PERF %s: n=%u total_ms=%llu p50_bin=%u p95_bin=%u p99_bin=%u max_ms=%u over20=%u",
            p_name, (unsigned int)p_timing->count,
            (unsigned long long)p_timing->total_ms,
            (unsigned int)percentile_bin(p_timing, 50),
            (unsigned int)percentile_bin(p_timing, 95),
            (unsigned int)percentile_bin(p_timing, 99),
            (unsigned int)p_timing->max_ms, (unsigned int)p_timing->over_budget);
}

static void log_stack(const char *p_name, const struct k_thread *p_thread, unsigned int index)
{
    size_t unused_bytes;
    const int ret = k_thread_stack_space_get(p_thread, &unused_bytes);
    if (ret != 0) {
        // Never present an unavailable scan as zero free bytes.
        LOG_INF("PERF stack %s: unavailable=%d", p_name, ret);
        return;
    }
    if (unused_bytes < min_unused[index]) {
        min_unused[index] = unused_bytes;
    }
    LOG_INF("PERF stack %s: unused=%u min_unused=%u", p_name,
            (unsigned int)unused_bytes, (unsigned int)min_unused[index]);
}

void perf_monitor_log_summary(void)
{
    (void)perf_monitor_get_stats(&snapshot);
    // Fields below share a snapshot; subsequent stack scans are independent samples.
    LOG_INF("PERF uptime_ms=%lld measurements=%u without_submit=%u submit_failed=%u invalid_time=%u",
            (long long)k_uptime_get(), (unsigned int)snapshot.measurements,
            (unsigned int)snapshot.without_submit, (unsigned int)snapshot.track_submit_failed,
            (unsigned int)snapshot.invalid_timestamps);
    log_timing("queue_wait", &snapshot.queue_wait);
    log_timing("processing", &snapshot.processing);
    log_timing("rx_to_submit", &snapshot.rx_to_submit);
    for (unsigned int i = 0; i < PERF_QUEUE_COUNT; ++i) {
        const struct perf_queue_stats *p_queue = &snapshot.queues[i];
        LOG_INF("PERF queue %s: accepted=%u rejected=%u observed_max_used=%u",
                i == PERF_QUEUE_RX ? "rx" : "tx", (unsigned int)p_queue->accepted,
                (unsigned int)p_queue->rejected, (unsigned int)p_queue->observed_max_used);
    }
    LOG_INF("PERF tx: poll_completed=%u encode_failed=%u",
            (unsigned int)snapshot.tx_poll_completed, (unsigned int)snapshot.tx_encode_failed);
    log_stack("rx", uart_rx_tid, 0);
    log_stack("handler", frame_handler_tid, 1);
    log_stack("tx", uart_tx_tid, 2);
}
