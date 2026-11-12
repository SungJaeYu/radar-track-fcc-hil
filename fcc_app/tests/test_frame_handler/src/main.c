#include "../../common/test_runner.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "protocol/status_message.h"
#include "protocol/frame_parser.h"

// Only the handler call is wrapped for failure injection; successful calls use
// the real encoder linked separately. No production source is changed.
static int test_status_encode(const struct rx_frame_stats *p_stats,
                              struct protocol_frame *out_frame);
#define status_message_encode test_status_encode
#include "application/frame_handler.c"
#undef status_message_encode

static char output[2048];
static size_t output_size;
static unsigned int stats_reads;
static struct rx_frame_stats source_stats;
static struct send_frame submitted_frame;
static unsigned int submit_calls;
static int submit_result;
static int encode_result;

static int test_status_encode(const struct rx_frame_stats *p_stats,
                              struct protocol_frame *out_frame)
{
    if (encode_result != 0) {
        return encode_result;
    }
    return status_message_encode(p_stats, out_frame);
}

// Capture a copy, never a pointer to the handler's local request. Queue scheduling
// and physical UART transmission belong to the separate TX/board test layers.
int uart_tx_submit(const struct send_frame *p_frame)
{
    assert(p_frame != NULL);
    submit_calls++;
    submitted_frame = *p_frame;
    return submit_result;
}

int printk(const char *p_format, ...)
{
    va_list args;
    va_start(args, p_format);
    int count = vsnprintf(output + output_size, sizeof(output) - output_size, p_format, args);
    va_end(args);
    assert(count >= 0 && (size_t)count < sizeof(output) - output_size);
    output_size += (size_t)count;
    return count;
}

// Processor counter behavior is covered by test_uart_rx; this fixture supplies
// distinct values so field swaps and accidental repeated reads are observable.
void rx_frame_processor_get_stats(struct rx_frame_stats *out_stats)
{
    stats_reads++;
    *out_stats = source_stats;
}

static void reset_test(struct frame_handler *p_handler)
{
    memset(output, 0, sizeof(output));
    output_size = stats_reads = 0;
    submit_calls = 0;
    submit_result = encode_result = 0;
    memset(&submitted_frame, 0, sizeof(submitted_frame));
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
    reset_test(&handler);
    request_status(&handler);
    assert(stats_reads == 1 && submit_calls == 1);
    assert(submitted_frame.frame.type == FRAME_TYPE_STATUS_REPORT);
    assert(submitted_frame.frame.payload_length == 16);
    const uint8_t expected[16] = {0};
    assert(memcmp(submitted_frame.frame.payload, expected, sizeof(expected)) == 0);
    assert(strcmp(output, "TYPE_STATUS\nCRC: 0, length: 0, timeout: 0, queue drops: 0\n") == 0);
}

static void test_status_snapshot(void)
{
    struct frame_handler handler;
    reset_test(&handler);
    source_stats = (struct rx_frame_stats){1, 23, 456, UINT32_MAX};
    request_status(&handler);
    assert(strcmp(output,
                  "TYPE_STATUS\nCRC: 1, length: 23, timeout: 456, queue drops: 4294967295\n") == 0);
    source_stats.crc_errors = 2;
    output_size = 0;
    request_status(&handler);
    assert(stats_reads == 2 && submit_calls == 2);
    const uint8_t expected[] = {0, 0, 0, 2, 0, 0, 0, 23, 0, 0, 1, 0xc8, 0xff, 0xff, 0xff, 0xff};
    assert(memcmp(submitted_frame.frame.payload, expected, sizeof(expected)) == 0);
    assert(strcmp(output,
                  "TYPE_STATUS\nCRC: 2, length: 23, timeout: 456, queue drops: 4294967295\n") == 0);
    assert(source_stats.length_errors == 23 && source_stats.frame_timeouts == 456);
    assert(source_stats.queue_drops == UINT32_MAX);
}

static void test_status_preserves_track(void)
{
    struct frame_handler handler;
    reset_test(&handler);
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
    reset_test(&handler);
    request_status(&handler);
    assert(!handler.timeout_armed && handler.last_valid_rx_ms == 0);
    frame_handler_check_timeout(&handler, 10000);
    assert(strstr(output, "track cleared") == NULL);
}

static void test_status_does_not_refresh_timeout(void)
{
    struct frame_handler handler;
    reset_test(&handler);
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

static void test_status_encode_wire(void)
{
    const struct rx_frame_stats stats = {0x12345678U, 0x80000000U, 1U, UINT32_MAX};
    struct protocol_frame frame;
    memset(&frame, 0xa5, sizeof(frame));
    assert(status_message_encode(&stats, &frame) == 0);
    assert(frame.type == FRAME_TYPE_STATUS_REPORT && frame.payload_length == 16);
    // Independently calculated with Python binascii.crc_hqx(body, 0xffff).
    const uint8_t expected[] = {0x7e, 0x10, 0x06, 0x12, 0x34, 0x56, 0x78, 0x80, 0x00, 0x00, 0x00,
                                0x00, 0x00, 0x00, 0x01, 0xff, 0xff, 0xff, 0xff, 0xeb, 0x11};
    uint8_t wire[sizeof(expected) + 1];
    memset(wire, 0xa5, sizeof(wire));
    assert(protocol_frame_encode(&frame, wire, sizeof(wire)) == (int)sizeof(expected));
    assert(memcmp(wire, expected, sizeof(expected)) == 0);
    assert(wire[sizeof(expected)] == 0xa5);
    for (size_t i = 16; i < sizeof(frame.payload); ++i) {
        assert(frame.payload[i] == 0xa5);
    }
    assert(stats.crc_errors == 0x12345678U && stats.length_errors == 0x80000000U);
    assert(stats.frame_timeouts == 1U && stats.queue_drops == UINT32_MAX);

    struct frame_parser parser;
    parser_init(&parser);
    for (size_t i = 0; i < sizeof(expected); ++i) {
        const ParserResult result = parser_feed(&parser, wire[i]);
        assert(result ==
               (i + 1 == sizeof(expected) ? PARSER_RESULT_FRAME_READY : PARSER_RESULT_INCOMPLETE));
    }
    assert(parser.frame.type == FRAME_TYPE_STATUS_REPORT);
    assert(parser.frame.payload_length == 16);
    assert(memcmp(parser.frame.payload, &expected[3], 16) == 0);
}

static void test_status_encode_invalid_arguments(void)
{
    const struct rx_frame_stats stats = {0};
    struct protocol_frame frame;
    memset(&frame, 0xa5, sizeof(frame));
    unsigned char before[sizeof(frame)];
    memcpy(before, &frame, sizeof(frame));
    assert(status_message_encode(NULL, &frame) == -EINVAL);
    assert(memcmp(before, &frame, sizeof(frame)) == 0);
    assert(status_message_encode(&stats, NULL) == -EINVAL);
    assert(status_message_encode(NULL, NULL) == -EINVAL);
}

static void test_status_submit_failure_and_recovery(void)
{
    const int errors[] = {-ENOMSG, -EINVAL, -EIO};
    for (size_t i = 0; i < sizeof(errors) / sizeof(errors[0]); ++i) {
        struct frame_handler handler;
        reset_test(&handler);
        track_start(&handler.track, 1000.0f, 0.25f, 100);
        handler.timeout_armed = true;
        handler.last_valid_rx_ms = 100;
        unsigned char before[sizeof(handler)];
        memcpy(before, &handler, sizeof(handler));
        source_stats.crc_errors = 0x12345678U;
        submit_result = errors[i];
        request_status(&handler);
        assert(submit_calls == 1 && stats_reads == 1);
        char expected[64];
        snprintf(expected, sizeof(expected), "STATUS submit failed: %d\n", errors[i]);
        assert(strstr(output, expected) != NULL);
        assert(memcmp(before, &handler, sizeof(handler)) == 0);
        assert(source_stats.crc_errors == 0x12345678U && source_stats.queue_drops == 0);

        output_size = 0;
        output[0] = '\0';
        submit_result = 0;
        request_status(&handler);
        assert(submit_calls == 2 && stats_reads == 2);
        assert(strstr(output, "failed") == NULL);
        assert(memcmp(before, &handler, sizeof(handler)) == 0);
    }
}

static void test_status_encode_failure_does_not_submit(void)
{
    struct frame_handler handler;
    reset_test(&handler);
    unsigned char before[sizeof(handler)];
    memcpy(before, &handler, sizeof(handler));
    encode_result = -EINVAL;
    request_status(&handler);
    assert(stats_reads == 1 && submit_calls == 0);
    char expected[64];
    snprintf(expected, sizeof(expected), "STATUS encode failed: %d\n", -EINVAL);
    assert(strstr(output, expected) != NULL);
    assert(memcmp(before, &handler, sizeof(handler)) == 0);

    encode_result = 0;
    request_status(&handler);
    assert(submit_calls == 1);
}

static void test_incoming_status_report_is_ignored(void)
{
    struct frame_handler handler;
    reset_test(&handler);
    unsigned char before[sizeof(handler)];
    memcpy(before, &handler, sizeof(handler));
    const struct received_frame frame = {
        .frame = {.type = FRAME_TYPE_STATUS_REPORT, .payload_length = 16},
        .received_at_ms = 900,
    };
    handle_frame(&handler, &frame);
    assert(stats_reads == 0 && submit_calls == 0);
    assert(strcmp(output, "Unexpected STATUS_REPORT received\n") == 0);
    assert(memcmp(before, &handler, sizeof(handler)) == 0);
}

static const struct test_case cases[] = {
    TEST_CASE(test_status_encode_wire),
    TEST_CASE(test_status_encode_invalid_arguments),
    TEST_CASE(test_status_submit_failure_and_recovery),
    TEST_CASE(test_status_encode_failure_does_not_submit),
    TEST_CASE(test_incoming_status_report_is_ignored),
    TEST_CASE(test_status_zero),
    TEST_CASE(test_status_snapshot),
    TEST_CASE(test_status_preserves_track),
    TEST_CASE(test_status_does_not_arm_timeout),
    TEST_CASE(test_status_does_not_refresh_timeout),
};

int main(int argc, char **p_argv)
{
    // The worker is compiled but is not scheduled by the host test.
    (void)frame_handler_thread;
    return run_tests(argc, p_argv, cases, sizeof(cases) / sizeof(cases[0]));
}
