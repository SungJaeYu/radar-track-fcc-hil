#include "../../common/test_runner.h"

#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>

#include "diagnostics/perf_monitor.h"
#include "diagnostics/perf_reporter.h"

static struct k_thread threads[3] = {{0}, {1}, {2}};
k_tid_t const uart_rx_tid = &threads[0];
k_tid_t const frame_handler_tid = &threads[1];
k_tid_t const uart_tx_tid = &threads[2];
static size_t stack_unused[3] = {700, 1200, 600};
static int stack_result[3];
static char output[8192];
static size_t output_size;

int64_t k_uptime_get(void)
{
    return 5000;
}

int k_thread_stack_space_get(const struct k_thread *p_thread, size_t *out_unused)
{
    const unsigned int index = p_thread->index;
    assert(index < 3);
    if (stack_result[index] == 0) {
        *out_unused = stack_unused[index];
    }
    return stack_result[index];
}

int printk(const char *p_format, ...)
{
    va_list args;
    va_start(args, p_format);
    const int count = vsnprintf(output + output_size, sizeof(output) - output_size, p_format, args);
    va_end(args);
    assert(count >= 0 && (size_t)count < sizeof(output) - output_size);
    output_size += (size_t)count;
    return count;
}

static void reset_test(void)
{
    output_size = 0;
    memset(output, 0, sizeof(output));
}

static void test_summary_and_stack_queries(void)
{
    reset_test();
    perf_reporter_log_summary();
    assert(strstr(output, "PERF queue_wait: n=0") != NULL);
    assert(strstr(output, "PERF stack handler: unused=1200 min_unused=1200") != NULL);
    assert(strstr(output, "PERF uptime_ms=5000") != NULL);
    // Exactly 100 samples make nearest-rank percentile boundaries explicit.
    for (unsigned int i = 1; i <= 100; ++i) {
        const int64_t delay = i <= 50 ? 1 : (i <= 95 ? 20 : 40);
        perf_monitor_begin_measurement(100, 100 + delay);
        perf_monitor_record_track_submit(100 + delay, 0);
        perf_monitor_end_measurement();
    }
    stack_unused[1] = 800;
    stack_result[2] = -ENOTSUP;
    reset_test();
    perf_reporter_log_summary();
    assert(strstr(output, "PERF queue_wait: n=100") != NULL);
    assert(strstr(output, "p50_bin=1 p95_bin=20 p99_bin=21 max_ms=40 over20=5") != NULL);
    assert(strstr(output, "PERF stack handler: unused=800 min_unused=800") != NULL);
    assert(strstr(output, "PERF stack tx: unavailable=-") != NULL);
    assert(strstr(output, "PERF stack tx: unused=") == NULL);
    stack_unused[1] = 1000;
    stack_result[2] = 0;
    reset_test();
    perf_reporter_log_summary();
    assert(strstr(output, "PERF stack handler: unused=1000 min_unused=800") != NULL);
    assert(strstr(output, "PERF stack tx: unused=600 min_unused=600") != NULL);
}

static const struct test_case cases[] = {
    TEST_CASE(test_summary_and_stack_queries),
};

int main(int argc, char **p_argv)
{
    return run_tests(argc, p_argv, cases, sizeof(cases) / sizeof(cases[0]));
}
