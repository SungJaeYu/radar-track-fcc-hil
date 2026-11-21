#include "diagnostics/perf_reporter.h"

#include <stddef.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/__assert.h>

#if defined(CONFIG_TIMING_FUNCTIONS)
#include <zephyr/timing/timing.h>
#endif

#include "diagnostics/perf_monitor.h"

// Bench-only summaries have a fixed INFO level, independent of per-frame DEBUG logs.
LOG_MODULE_REGISTER(radar_perf, LOG_LEVEL_INF);

extern const k_tid_t uart_rx_tid;
extern const k_tid_t frame_handler_tid;
extern const k_tid_t uart_tx_tid;

// Main is the sole caller. Keep the large snapshot off its small stack.
static struct perf_stats snapshot;
static size_t min_unused[3] = {SIZE_MAX, SIZE_MAX, SIZE_MAX};

static void log_execution(const char *p_name, const struct perf_execution_stats *p_stats);

static uint32_t percentile_bin(const struct perf_latency_stats *p_latency, uint32_t percentile)
{
    if (p_latency->sample_count == 0) {
        return 0;
    }
    const uint64_t rank = ((uint64_t)p_latency->sample_count * percentile + 99U) / 100U;
    uint64_t cumulative = 0;
    for (uint32_t i = 0; i < PERF_LATENCY_BIN_COUNT; ++i) {
        cumulative += p_latency->histogram_counts[i];
        if (cumulative >= rank) {
            return i;
        }
    }
    return PERF_LATENCY_BUDGET_MS + 1U;
}

static void log_latency(const char *p_name, const struct perf_latency_stats *p_latency)
{
    // Percentile bin 21 means >20 ms, not an exact 21 ms observation.
    LOG_INF("PERF %s: n=%u total_ms=%llu p50_bin=%u p95_bin=%u p99_bin=%u max_ms=%u over_budget_count=%u status=%s",
            p_name, (unsigned int)p_latency->sample_count,
            (unsigned long long)p_latency->total_ms,
            (unsigned int)percentile_bin(p_latency, 50),
            (unsigned int)percentile_bin(p_latency, 95),
            (unsigned int)percentile_bin(p_latency, 99),
            (unsigned int)p_latency->max_ms, (unsigned int)p_latency->over_budget_count,
            p_latency->sample_count == 0 ? "no_samples" : "ok");
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
    LOG_INF("PERF stack %s: unused_bytes=%u min_unused_bytes=%u", p_name,
            (unsigned int)unused_bytes, (unsigned int)min_unused[index]);
}

void perf_reporter_log_summary(void)
{
    (void)perf_monitor_get_stats(&snapshot);
    // Fields below share a snapshot; subsequent stack scans are independent samples.
    LOG_INF("PERF summary: uptime_ms=%lld measurements=%u without_submit=%u submit_failed=%u invalid_time=%u",
            (long long)k_uptime_get(), (unsigned int)snapshot.measurements,
            (unsigned int)snapshot.without_submit, (unsigned int)snapshot.track_submit_failed,
            (unsigned int)snapshot.invalid_timestamps);

    log_latency("queue_wait", &snapshot.queue_wait);
    log_latency("processing", &snapshot.processing);
    log_latency("rx_to_submit", &snapshot.rx_to_submit);

    log_execution("estimator_predict", &snapshot.estimator_predict);
    log_execution("estimator_correct", &snapshot.estimator_correct);

    for (unsigned int i = 0; i < PERF_QUEUE_COUNT; ++i) {
        const struct perf_queue_stats *p_queue = &snapshot.queues[i];
        LOG_INF("PERF queue %s: accepted=%u rejected=%u observed_max_used=%u",
                i == PERF_QUEUE_RX ? "rx" : "tx", (unsigned int)p_queue->accepted_count,
                (unsigned int)p_queue->rejected_count, (unsigned int)p_queue->max_observed_depth);
    }

    LOG_INF("PERF tx: poll_completed=%u encode_failed=%u",
            (unsigned int)snapshot.tx_poll_completed, (unsigned int)snapshot.tx_encode_failed);

    log_stack("rx", uart_rx_tid, 0);
    log_stack("handler", frame_handler_tid, 1);
    log_stack("tx", uart_tx_tid, 2);
}

static void log_execution(const char *p_name, const struct perf_execution_stats *p_stats)
{
    __ASSERT(p_name != NULL, "Execution label is NULL");
    __ASSERT(p_stats != NULL, "Execution statistics pointer is NULL");

    if (p_stats->sample_count == 0) {
        LOG_INF("PERF %s: n=0 status=no_samples", p_name);
        return;
    }

    const uint64_t mean_cycles = p_stats->total_cycles / (uint64_t)p_stats->sample_count;

#if defined(CONFIG_TIMING_FUNCTIONS)
    const uint64_t min_ns = timing_cycles_to_ns(p_stats->min_cycles);
    const uint64_t mean_ns = timing_cycles_to_ns(mean_cycles);
    const uint64_t max_ns = timing_cycles_to_ns(p_stats->max_cycles);

    LOG_INF("PERF %s: n=%u min_ns=%llu mean_ns=%llu max_ns=%llu",
            p_name, (unsigned int)p_stats->sample_count,
            (unsigned long long)min_ns, (unsigned long long)mean_ns, (unsigned long long)max_ns);
#else
    LOG_INF("PERF %s: n=%u min_cycles=%llu mean_cycles=%llu max_cycles=%llu",
            p_name, (unsigned int)p_stats->sample_count,
            (unsigned long long)p_stats->min_cycles, (unsigned long long)mean_cycles,
            (unsigned long long)p_stats->max_cycles);
#endif
}
