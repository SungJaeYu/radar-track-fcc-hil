#include "../../common/test_runner.h"

#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <string.h>

#include "diagnostics/perf_monitor.c"

// Include the collector to reset private state and test saturation without billions of calls.
// No test reset entry point is exposed by production firmware.
static void reset_test(void)
{
    memset(&stats, 0, sizeof(stats));
    memset(&measurement, 0, sizeof(measurement));
}
static struct perf_stats snapshot(void)
{
    struct perf_stats stats;
    assert(perf_monitor_get_stats(&stats) == 0);
    return stats;
}

static void test_timing_boundaries(void)
{
    reset_test();
    const uint32_t delays[] = {0, 1, 19, 20, 21};
    for (unsigned int i = 0; i < sizeof(delays) / sizeof(delays[0]); ++i) {
        perf_monitor_begin_measurement(100, 100 + delays[i]);
        perf_monitor_record_track_submit(102 + delays[i], 0);
        perf_monitor_end_measurement();
    }
    const struct perf_stats stats = snapshot();
    assert(stats.measurements == 5 && stats.without_submit == 0);
    assert(stats.queue_wait.sample_count == 5 && stats.queue_wait.total_ms == 61);
    assert(stats.queue_wait.max_ms == 21 && stats.queue_wait.over_budget_count == 1);
    assert(stats.queue_wait.histogram_counts[20] == 1 && stats.queue_wait.histogram_counts[21] == 1);
    assert(stats.processing.sample_count == 5 && stats.processing.total_ms == 10);
    assert(stats.rx_to_submit.total_ms == 71 && stats.rx_to_submit.max_ms == 23);
    assert(stats.rx_to_submit.over_budget_count == 3);
}

static void test_failed_and_missing_submit(void)
{
    reset_test();
    perf_monitor_begin_measurement(100, 105);
    perf_monitor_record_track_submit(110, -ENOMSG);
    perf_monitor_end_measurement();
    perf_monitor_begin_measurement(200, 205);
    perf_monitor_end_measurement();
    const struct perf_stats stats = snapshot();
    assert(stats.measurements == 2 && stats.queue_wait.sample_count == 2);
    assert(stats.track_submit_failed == 1 && stats.without_submit == 1);
    assert(stats.rx_to_submit.sample_count == 0 && stats.processing.sample_count == 0);
}

static void test_context_cleanup(void)
{
    reset_test();
    perf_monitor_begin_measurement(0, 5);
    perf_monitor_end_measurement();
    // A control/timeout report after a dropped measurement must not reuse its time.
    perf_monitor_record_track_submit(10, 0);
    perf_monitor_end_measurement();
    perf_monitor_begin_measurement(100, 101);
    perf_monitor_record_track_submit(102, 0);
    perf_monitor_record_track_submit(103, 0);
    perf_monitor_end_measurement();
    const struct perf_stats stats = snapshot();
    assert(stats.measurements == 2 && stats.without_submit == 1);
    assert(stats.rx_to_submit.sample_count == 1 && stats.rx_to_submit.total_ms == 2);
}

static void test_invalid_timestamps(void)
{
    reset_test();
    perf_monitor_begin_measurement(-1, 0);
    perf_monitor_record_track_submit(1, 0);
    perf_monitor_end_measurement();
    perf_monitor_begin_measurement(20, 19);
    perf_monitor_end_measurement();
    perf_monitor_begin_measurement(20, 21);
    perf_monitor_record_track_submit(20, 0);
    perf_monitor_end_measurement();
    const struct perf_stats stats = snapshot();
    assert(stats.invalid_timestamps == 3 && stats.measurements == 3);
    assert(stats.queue_wait.sample_count == 1 && stats.rx_to_submit.sample_count == 0);
}

static void test_queue_and_tx_counts(void)
{
    reset_test();
    perf_monitor_record_queue(PERF_QUEUE_RX, 0, 3);
    perf_monitor_record_queue(PERF_QUEUE_RX, -ENOMSG, 8);
    perf_monitor_record_queue(PERF_QUEUE_TX, 0, 1);
    perf_monitor_record_queue(PERF_QUEUE_TX, 0, 0);
    perf_monitor_record_queue(PERF_QUEUE_COUNT, 0, 100);
    perf_monitor_record_queue((PerfQueue)-1, 0, 100);
    perf_monitor_record_tx(true);
    perf_monitor_record_tx(false);
    const struct perf_stats stats = snapshot();
    assert(stats.queues[PERF_QUEUE_RX].accepted_count == 1);
    assert(stats.queues[PERF_QUEUE_RX].rejected_count == 1);
    assert(stats.queues[PERF_QUEUE_RX].max_observed_depth == 8);
    assert(stats.queues[PERF_QUEUE_TX].accepted_count == 2);
    assert(stats.queues[PERF_QUEUE_TX].max_observed_depth == 1);
    assert(stats.tx_poll_completed == 1 && stats.tx_encode_failed == 1);
}

static void test_snapshot_contract(void)
{
    reset_test();
    assert(perf_monitor_get_stats(NULL) == -EINVAL);
    const struct perf_stats before = snapshot();
    const struct perf_stats after = snapshot();
    assert(memcmp(&before, &after, sizeof(before)) == 0);
}

static void *update_queue(void *p_arg)
{
    (void)p_arg;
    for (unsigned int i = 0; i < 10000; ++i) {
        perf_monitor_record_queue(PERF_QUEUE_TX, 0, 8);
    }
    return NULL;
}

static void test_concurrent_snapshots(void)
{
    reset_test();
    pthread_t workers[2];
    for (unsigned int i = 0; i < 2; ++i) {
        assert(pthread_create(&workers[i], NULL, update_queue, NULL) == 0);
    }
    for (unsigned int i = 0; i < 10000; ++i) {
        const struct perf_stats stats = snapshot();
        assert(stats.queues[PERF_QUEUE_TX].accepted_count <= 20000);
        if (stats.queues[PERF_QUEUE_TX].accepted_count != 0) {
            assert(stats.queues[PERF_QUEUE_TX].max_observed_depth == 8);
        }
    }
    for (unsigned int i = 0; i < 2; ++i) {
        assert(pthread_join(workers[i], NULL) == 0);
    }
    assert(snapshot().queues[PERF_QUEUE_TX].accepted_count == 20000);
}

static void test_counter_saturation(void)
{
    reset_test();
    stats.measurements = UINT32_MAX;
    stats.without_submit = UINT32_MAX;
    stats.queues[PERF_QUEUE_RX].accepted_count = UINT32_MAX;
    stats.tx_poll_completed = UINT32_MAX;
    stats.queue_wait.sample_count = UINT32_MAX - 1U;
    stats.queue_wait.histogram_counts[0] = UINT32_MAX - 1U;
    perf_monitor_begin_measurement(0, INT64_MAX);
    perf_monitor_end_measurement();
    perf_monitor_begin_measurement(0, 1);
    perf_monitor_end_measurement();
    perf_monitor_record_queue(PERF_QUEUE_RX, 0, 2);
    perf_monitor_record_tx(true);
    const struct perf_stats captured = snapshot();
    assert(captured.measurements == UINT32_MAX && captured.without_submit == UINT32_MAX);
    assert(captured.queue_wait.sample_count == UINT32_MAX);
    assert(captured.queue_wait.histogram_counts[21] == 1 && captured.queue_wait.histogram_counts[1] == 0);
    assert(captured.queue_wait.max_ms == UINT32_MAX);
    assert(captured.queue_wait.total_ms == UINT32_MAX);
    assert(captured.queues[PERF_QUEUE_RX].accepted_count == UINT32_MAX);
    assert(captured.tx_poll_completed == UINT32_MAX);
}

static void test_execution_aggregation(void)
{
    reset_test();
    perf_monitor_record_predict(0);
    perf_monitor_record_predict(9);
    perf_monitor_record_correct(UINT64_MAX);

    const struct perf_stats captured = snapshot();
    assert(captured.estimator_predict.sample_count == 2);
    assert(captured.estimator_predict.total_cycles == 9);
    assert(captured.estimator_predict.min_cycles == 0);
    assert(captured.estimator_predict.max_cycles == 9);
    assert(captured.estimator_correct.min_cycles == UINT64_MAX);

    perf_monitor_record_correct(1);
    assert(stats.estimator_correct.sample_count == 1);
    assert(stats.estimator_correct.total_cycles == UINT64_MAX);
    assert(stats.estimator_correct.min_cycles == UINT64_MAX);
    assert(stats.estimator_correct.max_cycles == UINT64_MAX);

    stats.estimator_predict.sample_count = UINT32_MAX;
    perf_monitor_record_predict(100);
    assert(stats.estimator_predict.sample_count == UINT32_MAX);
    assert(stats.estimator_predict.total_cycles == 9);
    assert(stats.estimator_predict.min_cycles == 0);
    assert(stats.estimator_predict.max_cycles == 9);
}

static const struct test_case cases[] = {
    TEST_CASE(test_counter_saturation),
    TEST_CASE(test_execution_aggregation),
    TEST_CASE(test_timing_boundaries),
    TEST_CASE(test_failed_and_missing_submit),
    TEST_CASE(test_context_cleanup),
    TEST_CASE(test_invalid_timestamps),
    TEST_CASE(test_queue_and_tx_counts),
    TEST_CASE(test_snapshot_contract),
    TEST_CASE(test_concurrent_snapshots),
};

int main(int argc, char **p_argv)
{
    return run_tests(argc, p_argv, cases, sizeof(cases) / sizeof(cases[0]));
}
