#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

// Include the real handler to keep its private thread function compiled as well.
#include "application/frame_handler.c"

static char output[2048];
static size_t output_size;
static unsigned int stats_reads;
static struct rx_frame_stats source_stats;

int printk(const char *p_format, ...)
{
    va_list args;
    va_start(args, p_format);
    int count = vsnprintf(output + output_size, sizeof(output) - output_size,
                          p_format, args);
    va_end(args);
    assert(count >= 0 && (size_t)count < sizeof(output) - output_size);
    output_size += (size_t)count;
    return count;
}

// Processor counter behavior is covered by test_uart_rx; this fixture supplies
// distinct values so field swaps and accidental repeated reads are observable.
void rx_frame_processor_get_stats(struct rx_frame_stats *p_stats)
{
    stats_reads++;
    *p_stats = source_stats;
}

static void reset_fixture(struct frame_handler *p_handler)
{
    memset(output, 0, sizeof(output));
    output_size = stats_reads = 0;
    memset(&source_stats, 0, sizeof(source_stats));
    memset(p_handler, 0, sizeof(*p_handler));
    frame_handler_init(p_handler);
}

static void request_status(struct frame_handler *p_handler)
{
    const struct received_frame frame = {
        .frame = {.type = FRAME_TYPE_STATUS_REQUEST, .payload_length = 1, .payload = {0}},
        .received_at_ms = 900,
    };
    handle_frame(p_handler, &frame);
}

static void test_status_zero(void)
{
    struct frame_handler handler;
    reset_fixture(&handler);
    request_status(&handler);
    assert(stats_reads == 1);
    assert(strcmp(output, "TYPE_STATUS\nCRC: 0, length: 0, timeout: 0, queue drops: 0\n") == 0);
}

static void test_status_snapshot(void)
{
    struct frame_handler handler;
    reset_fixture(&handler);
    source_stats = (struct rx_frame_stats){1, 23, 456, UINT32_MAX};
    request_status(&handler);
    assert(strcmp(output, "TYPE_STATUS\nCRC: 1, length: 23, timeout: 456, queue drops: 4294967295\n") == 0);
    source_stats.crc_errors = 2;
    output_size = 0;
    request_status(&handler);
    assert(stats_reads == 2);
    assert(strcmp(output, "TYPE_STATUS\nCRC: 2, length: 23, timeout: 456, queue drops: 4294967295\n") == 0);
    assert(source_stats.length_errors == 23 && source_stats.frame_timeouts == 456);
    assert(source_stats.queue_drops == UINT32_MAX);
}

static void test_status_preserves_track(void)
{
    struct frame_handler handler;
    reset_fixture(&handler);
    track_start(&handler.track, 1000.0f, 0.25f, 100);
    handler.timeout_armed = true;
    handler.last_valid_rx_ms = 100;
    unsigned char before[sizeof(handler)];
    memcpy(before, &handler, sizeof(handler));
    request_status(&handler);
    assert(memcmp(before, &handler, sizeof(handler)) == 0);
}

static void test_status_does_not_arm_timeout(void)
{
    struct frame_handler handler;
    reset_fixture(&handler);
    request_status(&handler);
    assert(!handler.timeout_armed && handler.last_valid_rx_ms == 0);
    frame_handler_check_timeout(&handler, 10000);
    assert(strstr(output, "track cleared") == NULL);
}

static void test_status_does_not_refresh_timeout(void)
{
    struct frame_handler handler;
    reset_fixture(&handler);
    track_start(&handler.track, 1000.0f, 0.25f, 100);
    handler.timeout_armed = true;
    handler.last_valid_rx_ms = 100;
    request_status(&handler);
    frame_handler_check_timeout(&handler, 1099);
    assert(handler.timeout_armed);
    frame_handler_check_timeout(&handler, 1100);
    assert(!handler.timeout_armed && handler.track.state == TRACK_STATE_EMPTY);
    assert(strstr(output, "Radar measurement timeout: track cleared\n") != NULL);
}

int main(int argc, char **p_argv)
{
    (void)frame_handler_thread;
    assert(argc == 2);
#define RUN(test) if (strcmp(p_argv[1], #test) == 0) { test(); return 0; }
    RUN(test_status_zero)
    RUN(test_status_snapshot)
    RUN(test_status_preserves_track)
    RUN(test_status_does_not_arm_timeout)
    RUN(test_status_does_not_refresh_timeout)
    fprintf(stderr, "Unknown test: %s\n", p_argv[1]);
    return 2;
}
