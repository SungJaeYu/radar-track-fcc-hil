#include "../../common/test_runner.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "protocol/status_message.h"
#include "protocol/frame_parser.h"

// Only the handler call is wrapped for failure injection; successful calls use
// the real encoder linked separately. No production source is changed.
static int test_status_encode(const struct status_report *p_stats,
                              struct protocol_frame *out_frame);
#define status_message_encode test_status_encode
#include "application/frame_handler.c"
#undef status_message_encode

static char output[2048];
static size_t output_size;
static unsigned int stats_reads;
static struct rx_frame_stats source_stats;
static struct uart_rx_stats source_uart_stats;
static unsigned int uart_stats_reads;
static struct send_frame submitted_frame;
static unsigned int submit_calls;
static int submit_result;
static int encode_result;

static int test_status_encode(const struct status_report *p_stats,
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

void uart_rx_get_stats(struct uart_rx_stats *out_stats)
{
    uart_stats_reads++;
    *out_stats = source_uart_stats;
}

static void reset_test(struct frame_handler *p_handler)
{
    memset(output, 0, sizeof(output));
    output_size = stats_reads = uart_stats_reads = 0;
    memset(&source_uart_stats, 0, sizeof(source_uart_stats));
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
    assert(submitted_frame.frame.payload_length == 28);
    const uint8_t expected[28] = {0};
    assert(memcmp(submitted_frame.frame.payload, expected, sizeof(expected)) == 0);
    assert(strcmp(output, "STATUS report queued: payload=28 bytes\n") == 0);
}

static void test_status_snapshot(void)
{
    struct frame_handler handler;
    reset_test(&handler);
    source_stats = (struct rx_frame_stats){1, 23, 456, UINT32_MAX};
    source_uart_stats = (struct uart_rx_stats){7, 0x80000000U, UINT32_MAX};
    request_status(&handler);
    assert(strcmp(output,
                  "STATUS report queued: payload=28 bytes\n") == 0);
    source_stats.crc_errors = 2;
    output_size = 0;
    request_status(&handler);
    assert(stats_reads == 2 && uart_stats_reads == 2 && submit_calls == 2);
    const uint8_t expected[] = {0, 0, 0, 2, 0, 0, 0, 23, 0, 0, 1, 0xc8, 0xff, 0xff, 0xff, 0xff,
                                0, 0, 0, 7, 0x80, 0, 0, 0, 0xff, 0xff, 0xff, 0xff};
    assert(memcmp(submitted_frame.frame.payload, expected, sizeof(expected)) == 0);
    assert(strcmp(output,
                  "STATUS report queued: payload=28 bytes\n") == 0);
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
    frame_handler_check_timeout(&handler, 100 + CONFIG_RADAR_MEASUREMENT_TIMEOUT_MS - 1);
    assert(handler.timeout_armed);
    frame_handler_check_timeout(&handler, 100 + CONFIG_RADAR_MEASUREMENT_TIMEOUT_MS);
    assert(!handler.timeout_armed && handler.track.state == TRACK_STATE_EMPTY);
    assert(strstr(output, "Radar measurement timeout: track cleared\n") != NULL);
}

static void test_status_encode_wire(void)
{
    const struct status_report stats = {0x12345678U, 0x80000000U, 1U, UINT32_MAX, 7U, 0x01020304U, UINT32_MAX};
    struct protocol_frame frame;
    memset(&frame, 0xa5, sizeof(frame));
    assert(status_message_encode(&stats, &frame) == 0);
    assert(frame.type == FRAME_TYPE_STATUS_REPORT && frame.payload_length == 28);
    // Independently calculated with Python binascii.crc_hqx(body, 0xffff).
    const uint8_t expected[] = {0x7e, 0x1c, 0x06, 0x12, 0x34, 0x56, 0x78, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x07, 0x01, 0x02, 0x03, 0x04, 0xff, 0xff, 0xff, 0xff, 0xae, 0x34};
    uint8_t wire[sizeof(expected) + 1];
    memset(wire, 0xa5, sizeof(wire));
    assert(protocol_frame_encode(&frame, wire, sizeof(wire)) == (int)sizeof(expected));
    assert(memcmp(wire, expected, sizeof(expected)) == 0);
    assert(wire[sizeof(expected)] == 0xa5);
    for (size_t i = 28; i < sizeof(frame.payload); ++i) {
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
    assert(parser.frame.payload_length == 28);
    assert(memcmp(parser.frame.payload, &expected[3], 28) == 0);
}

static void test_status_encode_invalid_arguments(void)
{
    const struct status_report stats = {0};
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
        snprintf(expected, sizeof(expected), "STATUS submit failed: %d", errors[i]);
        assert(strstr(output, expected) != NULL);
        assert(strstr(output, "report queued") == NULL);
        assert((strstr(output, "TX queue full") != NULL) == (errors[i] == -ENOMSG));
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
    assert(strcmp(output, "STATUS_REPORT ignored: unexpected input\n") == 0);
    assert(memcmp(before, &handler, sizeof(handler)) == 0);
}

// Keep sender timestamps distinct from board receive times in timeout tests.
static struct received_frame measurement(uint32_t timestamp_ms, uint16_t range_m,
                                         int64_t received_at_ms)
{
    struct received_frame frame = {
        .frame = {.type = FRAME_TYPE_MEAS_REPORT, .payload_length = 10},
        .received_at_ms = received_at_ms,
    };
    frame.frame.payload[0] = (uint8_t)(timestamp_ms >> 24);
    frame.frame.payload[1] = (uint8_t)(timestamp_ms >> 16);
    frame.frame.payload[2] = (uint8_t)(timestamp_ms >> 8);
    frame.frame.payload[3] = (uint8_t)timestamp_ms;
    frame.frame.payload[4] = (uint8_t)(range_m >> 8);
    frame.frame.payload[5] = (uint8_t)range_m;
    // The remaining four zero bytes encode an azimuth of zero radians.
    return frame;
}

static void check_control_reset(FrameType type)
{
    struct frame_handler handler;
    reset_test(&handler);
    struct received_frame frame = measurement(100, 1000, 5000);
    handle_frame(&handler, &frame);
    assert(handler.timeout_armed && handler.track.state == TRACK_STATE_TENTATIVE);
    source_stats = (struct rx_frame_stats){1, 2, 3, 4};
    request_status(&handler);
    const struct protocol_frame before = submitted_frame.frame;

    frame.frame.type = type;
    frame.frame.payload_length = 1;
    handle_frame(&handler, &frame);

    assert(handler.track.state == TRACK_STATE_EMPTY);
    assert(handler.track.hit_count == 0 && handler.track.miss_count == 0);
    assert(!handler.timeout_armed && handler.last_valid_rx_ms == 0);
    frame_handler_check_timeout(&handler, 20000);
    assert(strstr(output, "Radar measurement timeout") == NULL);
    request_status(&handler);
    assert(submitted_frame.frame.payload_length == before.payload_length);
    assert(memcmp(submitted_frame.frame.payload, before.payload, before.payload_length) == 0);
    assert(stats_reads == 2 && uart_stats_reads == 2 && submit_calls == 2);

    // A new valid measurement rearms monitoring after either command.
    frame = measurement(0, 2000, 30000);
    handle_frame(&handler, &frame);
    assert(handler.timeout_armed && handler.last_valid_rx_ms == 30000);
    assert(handler.track.state == TRACK_STATE_TENTATIVE);
}

static void test_init_command_resets_tracking(void)
{
    check_control_reset(FRAME_TYPE_INIT);
}

static void test_reset_command_resets_tracking(void)
{
    check_control_reset(FRAME_TYPE_RESET);
}

static void test_valid_measurements_arm_refresh_and_expire(void)
{
    struct frame_handler handler;
    reset_test(&handler);
    frame_handler_check_timeout(&handler, 4000);
    assert(!handler.timeout_armed);
    assert(strstr(output, "Radar measurement timeout") == NULL);

    struct received_frame frame = measurement(100, 1000, 5000);
    handle_frame(&handler, &frame);
    assert(handler.timeout_armed && handler.last_valid_rx_ms == 5000);
    assert(handler.track.state == TRACK_STATE_TENTATIVE);
    output_size = 0;
    output[0] = '\0';
    frame = measurement(200, 1000, 5500);
    handle_frame(&handler, &frame);
    assert(handler.last_valid_rx_ms == 5500);
    assert(handler.track.hit_count == 2);
    // One input, one prediction, and one result; unchanged state is silent.
    size_t log_lines = 0;
    for (const char *p_char = output; *p_char != '\0'; ++p_char) {
        log_lines += *p_char == '\n';
    }
    assert(log_lines == 3);
    assert(strstr(output, "range_rate=") != NULL);
    assert(strstr(output, "Track state:") == NULL);
    frame_handler_check_timeout(&handler, 5500 + CONFIG_RADAR_MEASUREMENT_TIMEOUT_MS / 2);
    assert(handler.timeout_armed);
    frame_handler_check_timeout(&handler, 5500 + CONFIG_RADAR_MEASUREMENT_TIMEOUT_MS - 1);
    assert(handler.timeout_armed);
    frame_handler_check_timeout(&handler, 5500 + CONFIG_RADAR_MEASUREMENT_TIMEOUT_MS);
    assert(!handler.timeout_armed && handler.track.state == TRACK_STATE_EMPTY);
    assert(strstr(output, "Radar measurement timeout: track cleared\n") != NULL);
    const size_t log_size = output_size;
    frame_handler_check_timeout(&handler, 9000);
    assert(output_size == log_size);
}

static void test_invalid_measurements_do_not_arm_timeout(void)
{
    // Cover decode failure and a decoded but invalid zero range separately.
    for (int invalid_length = 0; invalid_length < 2; ++invalid_length) {
        struct frame_handler handler;
        reset_test(&handler);
        struct received_frame frame = measurement(100, invalid_length ? 1000 : 0, 5000);
        if (invalid_length) {
            frame.frame.payload_length = 9;
        }
        handle_frame(&handler, &frame);
        assert(strstr(output, invalid_length ? "Invalid radar payload"
                                             : "Invalid radar measurement") != NULL);
        assert(!handler.timeout_armed && handler.last_valid_rx_ms == 0);
        assert(handler.track.state == TRACK_STATE_EMPTY);
        frame_handler_check_timeout(&handler, 10000);
        assert(strstr(output, "Radar measurement timeout") == NULL);
    }
}

static void test_invalid_measurements_do_not_refresh_timeout(void)
{
    for (int invalid_length = 0; invalid_length < 2; ++invalid_length) {
        struct frame_handler handler;
        reset_test(&handler);
        struct received_frame frame = measurement(100, 1000, 5000);
        handle_frame(&handler, &frame);
        unsigned char before[sizeof(handler.track)];
        memcpy(before, &handler.track, sizeof(before));
        frame = measurement(200, invalid_length ? 1000 : 0, 5900);
        if (invalid_length) {
            frame.frame.payload_length = 11;
        }
        handle_frame(&handler, &frame);
        assert(handler.last_valid_rx_ms == 5000 && handler.timeout_armed);
        assert(memcmp(before, &handler.track, sizeof(before)) == 0);
        frame_handler_check_timeout(&handler, 5000 + CONFIG_RADAR_MEASUREMENT_TIMEOUT_MS - 1);
        assert(handler.timeout_armed);
        frame_handler_check_timeout(&handler, 5000 + CONFIG_RADAR_MEASUREMENT_TIMEOUT_MS);
        assert(!handler.timeout_armed && handler.track.state == TRACK_STATE_EMPTY);
    }
}

static void test_gate_rejection_refreshes_receive_timeout(void)
{
    struct frame_handler handler;
    reset_test(&handler);
    for (uint32_t i = 0; i < 3; ++i) {
        struct received_frame frame = measurement(100 + i * 100, 1000, 5000 + i * 100);
        handle_frame(&handler, &frame);
    }
    assert(handler.track.state == TRACK_STATE_CONFIRMED);
    assert(strstr(output, "Track state: TENTATIVE -> CONFIRMED") != NULL);
    struct received_frame frame = measurement(400, 2000, 5900);
    handle_frame(&handler, &frame);
    assert(strstr(output, "Measurement rejected by gate") != NULL);
    assert(strstr(output, "Track state: CONFIRMED -> COASTING") != NULL);
    assert(handler.track.state == TRACK_STATE_COASTING && handler.track.miss_count == 1);
    assert(handler.timeout_armed && handler.last_valid_rx_ms == 5900);
    frame_handler_check_timeout(&handler, 5900 + CONFIG_RADAR_MEASUREMENT_TIMEOUT_MS - 1);
    assert(handler.timeout_armed);
    frame_handler_check_timeout(&handler, 5900 + CONFIG_RADAR_MEASUREMENT_TIMEOUT_MS);
    assert(!handler.timeout_armed && handler.track.state == TRACK_STATE_EMPTY);
}

static void test_timestamp_rejection_refreshes_receive_timeout(void)
{
    const uint32_t timestamps[] = {100, 99};
    for (size_t i = 0; i < sizeof(timestamps) / sizeof(timestamps[0]); ++i) {
        struct frame_handler handler;
        reset_test(&handler);
        struct received_frame frame = measurement(100, 1000, 5000);
        handle_frame(&handler, &frame);
        unsigned char before[sizeof(handler.track)];
        memcpy(before, &handler.track, sizeof(before));
        frame = measurement(timestamps[i], 1000, 5900);
        handle_frame(&handler, &frame);
        assert(strstr(output, "Prediction skipped: previous_t=100 ms") != NULL);
        assert(memcmp(before, &handler.track, sizeof(before)) == 0);
        assert(handler.timeout_armed && handler.last_valid_rx_ms == 5900);
        frame_handler_check_timeout(&handler, 5900 + CONFIG_RADAR_MEASUREMENT_TIMEOUT_MS - 1);
        assert(handler.timeout_armed);
        frame_handler_check_timeout(&handler, 5900 + CONFIG_RADAR_MEASUREMENT_TIMEOUT_MS);
        assert(!handler.timeout_armed);
    }
}

static const struct test_case cases[] = {
    TEST_CASE(test_init_command_resets_tracking),
    TEST_CASE(test_reset_command_resets_tracking),
    TEST_CASE(test_valid_measurements_arm_refresh_and_expire),
    TEST_CASE(test_invalid_measurements_do_not_arm_timeout),
    TEST_CASE(test_invalid_measurements_do_not_refresh_timeout),
    TEST_CASE(test_gate_rejection_refreshes_receive_timeout),
    TEST_CASE(test_timestamp_rejection_refreshes_receive_timeout),

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
