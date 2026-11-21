#ifndef PERF_MONITOR_H
#define PERF_MONITOR_H

#include <stdbool.h>
#include <stdint.h>

// Histogram indices 0..20 represent exact millisecond values; index 21 counts values above the budget.
#define PERF_LATENCY_BUDGET_MS 20U
#define PERF_LATENCY_BIN_COUNT (PERF_LATENCY_BUDGET_MS + 2U)

typedef enum {
    PERF_QUEUE_RX,
    PERF_QUEUE_TX,
    PERF_QUEUE_COUNT,
} PerfQueue;

struct perf_latency_stats {
    uint32_t sample_count;
    uint64_t total_ms;
    uint32_t max_ms;
    uint32_t over_budget_count;
    uint32_t histogram_counts[PERF_LATENCY_BIN_COUNT];
};

struct perf_queue_stats {
    uint32_t accepted_count;
    uint32_t rejected_count;
    // Sampled after submission; the consumer may already have removed an item.
    uint32_t max_observed_depth;
};

// Execution costs in timer cycles; min/max are meaningful only when sample_count is nonzero.
struct perf_execution_stats {
    uint32_t sample_count;
    uint64_t total_cycles;
    uint64_t min_cycles;
    uint64_t max_cycles;
};

struct perf_stats {
    struct perf_latency_stats queue_wait;
    struct perf_latency_stats processing;
    struct perf_latency_stats rx_to_submit;
    struct perf_queue_stats queues[PERF_QUEUE_COUNT];
    struct perf_execution_stats estimator_predict;
    struct perf_execution_stats estimator_correct;
    uint32_t measurements;
    uint32_t without_submit;
    uint32_t track_submit_failed;
    uint32_t tx_poll_completed;
    uint32_t tx_encode_failed;
    uint32_t invalid_timestamps;
};

// All time arguments use board uptime in milliseconds, not sensor timestamps.
// Begin/end and track submission run only in the handler thread, without nesting.
// End every measurement, including stale/invalid input and encode failure paths.
void perf_monitor_begin_measurement(int64_t received_uptime_ms, int64_t handler_start_uptime_ms);
void perf_monitor_record_track_submit(int64_t submit_uptime_ms, int result);
void perf_monitor_end_measurement(void);

// Queue hooks may run concurrently. Invalid API arguments are excluded by callers.
void perf_monitor_record_queue(PerfQueue queue, int result, uint32_t used);
// True means all uart_poll_out calls returned, not peer reception or wire idle.
void perf_monitor_record_tx(bool completed);

// Samples that would overflow the count or total are excluded without changing the aggregate.
void perf_monitor_record_predict(uint64_t elapsed_cycles);
void perf_monitor_record_correct(uint64_t elapsed_cycles);

// Copy one protected snapshot. Return 0 on success or -EINVAL for NULL.
// Counters saturate; timing aggregates stop accepting samples at UINT32_MAX.
// INIT/RESET preserve these boot-lifetime diagnostics.
int perf_monitor_get_stats(struct perf_stats *out_stats);

#endif
