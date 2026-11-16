#include "../../common/test_runner.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "protocol/status_message.h"
#include "protocol/track_message.h"
#include "protocol/frame_parser.h"

// Only the handler call is wrapped for failure injection; successful calls use
// the real encoder linked separately. No production source is changed.
static int test_status_encode(const struct status_report *p_stats,
                              struct protocol_frame *out_frame);
static int test_track_encode(const struct track_report *p_report,
                             struct protocol_frame *out_frame);
#define track_message_encode test_track_encode
#define status_message_encode test_status_encode
#include "application/report_sender.c"
#undef status_message_encode
#undef track_message_encode
#include "application/frame_handler.c"

static char output[2048];
static size_t output_size;
static unsigned int stats_reads;
static struct rx_frame_processor_stats source_stats;
static struct uart_rx_stats source_uart_stats;
static unsigned int uart_stats_reads;
static struct tx_frame submitted_frame;
static unsigned int submit_calls;
static int submit_result;
static int encode_result;
static int track_encode_result;

static int test_track_encode(const struct track_report *p_report,
                             struct protocol_frame *out_frame)
{
    if (track_encode_result != 0) {
        return track_encode_result;
    }
    return track_message_encode(p_report, out_frame);
}


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
int uart_tx_submit(const struct tx_frame *p_frame)
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
void rx_frame_processor_get_stats(struct rx_frame_processor_stats *out_stats)
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
    submit_result = encode_result = track_encode_result = 0;
    memset(&submitted_frame, 0, sizeof(submitted_frame));
    memset(&source_stats, 0, sizeof(source_stats));
    memset(p_handler, 0, sizeof(*p_handler));
    frame_handler_init(p_handler);
}

static void request_status(struct frame_handler *p_handler)
{
    const struct rx_frame frame = {
        .frame = {.type = FRAME_TYPE_STATUS_REQUEST, .payload_length = 1, .payload = {0}},
        .received_at_ms = 900,
    };
    frame_handler_process(p_handler, &frame);
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
    source_stats = (struct rx_frame_processor_stats){1, 23, 456, UINT32_MAX};
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
    track_start(&handler.radar.track, 1000.0f, 0.25f, 100);
    handler.radar.timeout_armed = true;
    handler.radar.last_valid_rx_ms = 100;
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
    assert(!handler.radar.timeout_armed && handler.radar.last_valid_rx_ms == 0);
    frame_handler_check_timeout(&handler, 10000);
    assert(strstr(output, "track cleared") == NULL);
}

static void test_status_does_not_refresh_timeout(void)
{
    struct frame_handler handler;
    reset_test(&handler);
    track_start(&handler.radar.track, 1000.0f, 0.25f, 100);
    handler.radar.timeout_armed = true;
    handler.radar.last_valid_rx_ms = 100;
    request_status(&handler);
    frame_handler_check_timeout(&handler, 100 + CONFIG_RADAR_MEASUREMENT_TIMEOUT_MS - 1);
    assert(handler.radar.timeout_armed);
    frame_handler_check_timeout(&handler, 100 + CONFIG_RADAR_MEASUREMENT_TIMEOUT_MS);
    assert(!handler.radar.timeout_armed && handler.radar.track.state == TRACK_STATE_EMPTY);
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
    frame_parser_init(&parser);
    for (size_t i = 0; i < sizeof(expected); ++i) {
        const ParserResult result = frame_parser_feed(&parser, wire[i]);
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
        track_start(&handler.radar.track, 1000.0f, 0.25f, 100);
        handler.radar.timeout_armed = true;
        handler.radar.last_valid_rx_ms = 100;
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
    const struct rx_frame frame = {
        .frame = {.type = FRAME_TYPE_STATUS_REPORT, .payload_length = 16},
        .received_at_ms = 900,
    };
    frame_handler_process(&handler, &frame);
    assert(stats_reads == 0 && submit_calls == 0);
    assert(strcmp(output, "STATUS_REPORT ignored: unexpected input\n") == 0);
    assert(memcmp(before, &handler, sizeof(handler)) == 0);
}

// Keep sender timestamps distinct from board receive times in timeout tests.
static struct rx_frame measurement(uint32_t timestamp_ms, uint16_t range_m,
                                         int64_t received_at_ms)
{
    struct rx_frame frame = {
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
    struct rx_frame frame = measurement(100, 1000, 5000);
    frame_handler_process(&handler, &frame);
    assert(handler.radar.timeout_armed && handler.radar.track.state == TRACK_STATE_TENTATIVE);
    source_stats = (struct rx_frame_processor_stats){1, 2, 3, 4};
    request_status(&handler);
    const struct protocol_frame before = submitted_frame.frame;

    frame.frame.type = type;
    frame.frame.payload_length = 1;
    frame_handler_process(&handler, &frame);

    assert(handler.radar.track.state == TRACK_STATE_EMPTY);
    assert(handler.radar.track.hit_count == 0 && handler.radar.track.miss_count == 0);
    assert(!handler.radar.timeout_armed && handler.radar.last_valid_rx_ms == 0);
    frame_handler_check_timeout(&handler, 20000);
    assert(strstr(output, "Radar measurement timeout") == NULL);
    request_status(&handler);
    assert(submitted_frame.frame.payload_length == before.payload_length);
    assert(memcmp(submitted_frame.frame.payload, before.payload, before.payload_length) == 0);
    assert(stats_reads == 2 && uart_stats_reads == 2 && submit_calls == 3);

    assert(handler.report_sender.next_track_report_sequence == 1);

    // A new valid measurement rearms monitoring after either command.
    frame = measurement(0, 2000, 30000);
    frame_handler_process(&handler, &frame);
    assert(handler.radar.timeout_armed && handler.radar.last_valid_rx_ms == 30000);
    assert(handler.radar.track.state == TRACK_STATE_TENTATIVE);
    assert(handler.report_sender.next_track_report_sequence == 2);
    assert(submitted_frame.frame.payload[3] == 1);
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
    assert(!handler.radar.timeout_armed);
    assert(strstr(output, "Radar measurement timeout") == NULL);

    struct rx_frame frame = measurement(100, 1000, 5000);
    frame_handler_process(&handler, &frame);
    assert(handler.radar.timeout_armed && handler.radar.last_valid_rx_ms == 5000);
    assert(handler.radar.track.state == TRACK_STATE_TENTATIVE);
    output_size = 0;
    output[0] = '\0';
    frame = measurement(200, 1000, 5500);
    frame_handler_process(&handler, &frame);
    assert(handler.radar.last_valid_rx_ms == 5500);
    assert(handler.radar.track.hit_count == 2);
    // Input, prediction, result, and TX queue acceptance; unchanged state is silent.
    size_t log_lines = 0;
    for (const char *p_char = output; *p_char != '\0'; ++p_char) {
        log_lines += *p_char == '\n';
    }
    assert(log_lines == 4);
    assert(strstr(output, "TRACK report queued: payload=32 bytes") != NULL);
    assert(strstr(output, "range_rate=") != NULL);
    assert(strstr(output, "Track state:") == NULL);
    frame_handler_check_timeout(&handler, 5500 + CONFIG_RADAR_MEASUREMENT_TIMEOUT_MS / 2);
    assert(handler.radar.timeout_armed);
    frame_handler_check_timeout(&handler, 5500 + CONFIG_RADAR_MEASUREMENT_TIMEOUT_MS - 1);
    assert(handler.radar.timeout_armed);
    frame_handler_check_timeout(&handler, 5500 + CONFIG_RADAR_MEASUREMENT_TIMEOUT_MS);
    assert(!handler.radar.timeout_armed && handler.radar.track.state == TRACK_STATE_EMPTY);
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
        struct rx_frame frame = measurement(100, invalid_length ? 1000 : 0, 5000);
        if (invalid_length) {
            frame.frame.payload_length = 9;
        }
        frame_handler_process(&handler, &frame);
        assert(strstr(output, invalid_length ? "Invalid radar payload"
                                             : "Invalid radar measurement") != NULL);
        assert(!handler.radar.timeout_armed && handler.radar.last_valid_rx_ms == 0);
        assert(handler.radar.track.state == TRACK_STATE_EMPTY);
        assert(submit_calls == 0 && handler.report_sender.next_track_report_sequence == 0);
        frame_handler_check_timeout(&handler, 10000);
        assert(strstr(output, "Radar measurement timeout") == NULL);
    }
}

static void test_invalid_measurements_do_not_refresh_timeout(void)
{
    for (int invalid_length = 0; invalid_length < 2; ++invalid_length) {
        struct frame_handler handler;
        reset_test(&handler);
        struct rx_frame frame = measurement(100, 1000, 5000);
        frame_handler_process(&handler, &frame);
        unsigned char before[sizeof(handler.radar.track)];
        memcpy(before, &handler.radar.track, sizeof(before));
        frame = measurement(200, invalid_length ? 1000 : 0, 5900);
        if (invalid_length) {
            frame.frame.payload_length = 11;
        }
        frame_handler_process(&handler, &frame);
        assert(handler.radar.last_valid_rx_ms == 5000 && handler.radar.timeout_armed);
        assert(memcmp(before, &handler.radar.track, sizeof(before)) == 0);
        assert(submit_calls == 1 && handler.report_sender.next_track_report_sequence == 1);
        frame_handler_check_timeout(&handler, 5000 + CONFIG_RADAR_MEASUREMENT_TIMEOUT_MS - 1);
        assert(handler.radar.timeout_armed);
        frame_handler_check_timeout(&handler, 5000 + CONFIG_RADAR_MEASUREMENT_TIMEOUT_MS);
        assert(!handler.radar.timeout_armed && handler.radar.track.state == TRACK_STATE_EMPTY);
    }
}

static void test_gate_rejection_refreshes_receive_timeout(void)
{
    struct frame_handler handler;
    reset_test(&handler);
    for (uint32_t i = 0; i < 3; ++i) {
        struct rx_frame frame = measurement(100 + i * 100, 1000, 5000 + i * 100);
        frame_handler_process(&handler, &frame);
    }
    assert(handler.radar.track.state == TRACK_STATE_CONFIRMED);
    assert(strstr(output, "Track state: TENTATIVE -> CONFIRMED") != NULL);
    struct rx_frame frame = measurement(400, 2000, 5900);
    frame_handler_process(&handler, &frame);
    assert(strstr(output, "Measurement rejected by gate") != NULL);
    assert(strstr(output, "Track state: CONFIRMED -> COASTING") != NULL);
    assert(handler.radar.track.state == TRACK_STATE_COASTING && handler.radar.track.miss_count == 1);
    assert(handler.radar.timeout_armed && handler.radar.last_valid_rx_ms == 5900);
    frame_handler_check_timeout(&handler, 5900 + CONFIG_RADAR_MEASUREMENT_TIMEOUT_MS - 1);
    assert(handler.radar.timeout_armed);
    frame_handler_check_timeout(&handler, 5900 + CONFIG_RADAR_MEASUREMENT_TIMEOUT_MS);
    assert(!handler.radar.timeout_armed && handler.radar.track.state == TRACK_STATE_EMPTY);
}

static void test_timestamp_rejection_refreshes_receive_timeout(void)
{
    const uint32_t timestamps[] = {100, 99};
    for (size_t i = 0; i < sizeof(timestamps) / sizeof(timestamps[0]); ++i) {
        struct frame_handler handler;
        reset_test(&handler);
        struct rx_frame frame = measurement(100, 1000, 5000);
        frame_handler_process(&handler, &frame);
        unsigned char before[sizeof(handler.radar.track)];
        memcpy(before, &handler.radar.track, sizeof(before));
        frame = measurement(timestamps[i], 1000, 5900);
        frame_handler_process(&handler, &frame);
        assert(strstr(output, "Prediction skipped: previous_t=100 ms") != NULL);
        assert(memcmp(before, &handler.radar.track, sizeof(before)) == 0);
        assert(handler.radar.timeout_armed && handler.radar.last_valid_rx_ms == 5900);
        frame_handler_check_timeout(&handler, 5900 + CONFIG_RADAR_MEASUREMENT_TIMEOUT_MS - 1);
        assert(handler.radar.timeout_armed);
        frame_handler_check_timeout(&handler, 5900 + CONFIG_RADAR_MEASUREMENT_TIMEOUT_MS);
        assert(!handler.radar.timeout_armed);
    }
}

// Decode independently of the production writers to inspect captured wire fields.
static uint32_t report_u32(size_t offset)
{
    const uint8_t *p_bytes = &submitted_frame.frame.payload[offset];
    return ((uint32_t)p_bytes[0] << 24) | ((uint32_t)p_bytes[1] << 16) |
           ((uint32_t)p_bytes[2] << 8) | p_bytes[3];
}

static void check_track_report(uint32_t sequence, uint32_t input_ms, uint32_t estimate_ms,
                               uint8_t state, uint8_t result, bool estimate_valid)
{
    assert(submitted_frame.frame.type == FRAME_TYPE_TRACK_REPORT);
    assert(submitted_frame.frame.payload_length == 32);
    assert(report_u32(0) == sequence && report_u32(4) == input_ms);
    assert(report_u32(8) == estimate_ms);
    assert(submitted_frame.frame.payload[28] == state);
    assert(submitted_frame.frame.payload[29] == result);
    assert(submitted_frame.frame.payload[30] == (estimate_valid ? 3 : 1));
    assert(submitted_frame.frame.payload[31] == 0);
    if (!estimate_valid) {
        for (size_t i = 12; i < 28; ++i) {
            assert(submitted_frame.frame.payload[i] == 0);
        }
    }
}

static void feed_measurement(struct frame_handler *p_handler, uint32_t timestamp_ms,
                             uint16_t range_m)
{
    // Keep each action's log separate; do not hide per-action buffer overruns.
    output_size = 0;
    output[0] = '\0';
    const struct rx_frame frame = measurement(timestamp_ms, range_m, 5000 + timestamp_ms);
    frame_handler_process(p_handler, &frame);
}

static void test_track_encode_wire(void)
{
    const struct track_report report = {
        .report_sequence = 0x12345678U, .input_timestamp_ms = 0x80000000U,
        .estimate_timestamp_ms = UINT32_MAX, .range_m = 1.0f, .azimuth_rad = -2.5f,
        .range_rate_mps = 0.125f, .azimuth_rate_radps = -0.0f,
        .state = 2, .result = 1, .flags = 3,
    };
    // Python struct.pack('>IIIffffBBBB', ...) and binascii.crc_hqx(body, 0xffff).
    const uint8_t expected[] = {
        0x7e, 0x20, 0x07, 0x12, 0x34, 0x56, 0x78, 0x80, 0, 0, 0,
        0xff, 0xff, 0xff, 0xff, 0x3f, 0x80, 0, 0, 0xc0, 0x20, 0, 0,
        0x3e, 0, 0, 0, 0x80, 0, 0, 0, 2, 1, 3, 0, 0x8d, 0x42,
    };
    struct protocol_frame frame;
    assert(track_message_encode(&report, &frame) == 0);
    uint8_t wire[39];
    memset(wire, 0xa5, sizeof(wire));
    // An unaligned destination must work without overwriting either guard byte.
    assert(protocol_frame_encode(&frame, &wire[1], 37) == 37);
    assert(wire[0] == 0xa5 && wire[38] == 0xa5);
    assert(memcmp(&wire[1], expected, sizeof(expected)) == 0);
    struct frame_parser parser;
    frame_parser_init(&parser);
    for (size_t i = 0; i < sizeof(expected); ++i) {
        assert(frame_parser_feed(&parser, wire[i + 1]) ==
               (i == 36 ? PARSER_RESULT_FRAME_READY : PARSER_RESULT_INCOMPLETE));
    }
    assert(parser.frame.type == FRAME_TYPE_TRACK_REPORT);
    assert(parser.frame.payload_length == 32);
    assert(memcmp(parser.frame.payload, &expected[3], 32) == 0);
}

static void test_track_encode_invalid_arguments(void)
{
    const struct track_report report = {0};
    struct protocol_frame frame;
    memset(&frame, 0xa5, sizeof(frame));
    unsigned char before[sizeof(frame)];
    memcpy(before, &frame, sizeof(frame));
    assert(track_message_encode(NULL, &frame) == -EINVAL);
    assert(memcmp(before, &frame, sizeof(frame)) == 0);
    assert(track_message_encode(&report, NULL) == -EINVAL);
    assert(track_message_encode(NULL, NULL) == -EINVAL);
}

static void test_track_started_updated_and_confirmed(void)
{
    struct frame_handler handler;
    reset_test(&handler);
    feed_measurement(&handler, 100, 1000);
    check_track_report(0, 100, 100, TRACK_STATE_TENTATIVE, TRACK_REPORT_RESULT_STARTED, true);
    assert(report_u32(12) == 0x447a0000U); // 1000.0f
    assert(report_u32(16) == 0 && report_u32(20) == 0 && report_u32(24) == 0);
    feed_measurement(&handler, 200, 1010);
    check_track_report(1, 200, 200, TRACK_STATE_TENTATIVE, TRACK_REPORT_RESULT_UPDATED, true);
    // A corrected estimate differs from the prior stationary prediction.
    assert(report_u32(12) != 0x447a0000U);
    uint32_t corrected_bits;
    memcpy(&corrected_bits, &handler.radar.track.range_filter.estimate, sizeof(corrected_bits));
    assert(report_u32(12) == corrected_bits);
    feed_measurement(&handler, 300, 1010);
    check_track_report(2, 300, 300, TRACK_STATE_CONFIRMED, TRACK_REPORT_RESULT_UPDATED, true);
    assert(submit_calls == 3 && handler.report_sender.next_track_report_sequence == 3);
}

static void test_track_timestamp_rejection_report(void)
{
    const uint32_t times[] = {100, 99};
    for (size_t i = 0; i < 2; ++i) {
        struct frame_handler handler;
        reset_test(&handler);
        feed_measurement(&handler, 100, 1000);
        feed_measurement(&handler, times[i], 1000);
        check_track_report(1, times[i], 100, TRACK_STATE_TENTATIVE,
                           TRACK_REPORT_RESULT_TIME_REJECTED, true);
        assert(submit_calls == 2);
    }
}

static void test_track_gate_rejection_empty_and_lost(void)
{
    struct frame_handler handler;
    reset_test(&handler);
    feed_measurement(&handler, 100, 1000);
    feed_measurement(&handler, 200, 2000);
    check_track_report(1, 200, 0, TRACK_STATE_EMPTY, TRACK_REPORT_RESULT_GATE_REJECTED, false);

    reset_test(&handler);
    for (uint32_t i = 1; i <= 3; ++i) {
        feed_measurement(&handler, i * 100, 1000);
    }
    for (uint32_t i = 4; i <= 8; ++i) {
        feed_measurement(&handler, i * 100, 2000);
        const bool valid = i != 8;
        check_track_report(i - 1, i * 100, valid ? i * 100 : 0,
                           valid ? TRACK_STATE_COASTING : TRACK_STATE_LOST,
                           TRACK_REPORT_RESULT_GATE_REJECTED, valid);
    }
    feed_measurement(&handler, 900, 1000);
    check_track_report(8, 900, 0, TRACK_STATE_EMPTY, TRACK_REPORT_RESULT_RESET, false);
    feed_measurement(&handler, 1000, 1000);
    check_track_report(9, 1000, 1000, TRACK_STATE_TENTATIVE, TRACK_REPORT_RESULT_STARTED, true);
}

static void test_track_gap_reset_and_recovery(void)
{
    struct frame_handler handler;
    reset_test(&handler);
    feed_measurement(&handler, 100, 1000);
    feed_measurement(&handler, 1101, 1000);
    check_track_report(1, 1101, 0, TRACK_STATE_EMPTY, TRACK_REPORT_RESULT_RESET, false);
    feed_measurement(&handler, 1201, 1000);
    check_track_report(2, 1201, 1201, TRACK_STATE_TENTATIVE, TRACK_REPORT_RESULT_STARTED, true);
}

static void test_track_sequence_wrap_and_timeout_preservation(void)
{
    struct frame_handler handler;
    reset_test(&handler);
    handler.report_sender.next_track_report_sequence = UINT32_MAX;
    feed_measurement(&handler, UINT32_MAX - 49U, 1000);
    check_track_report(UINT32_MAX, UINT32_MAX - 49U, UINT32_MAX - 49U,
                       TRACK_STATE_TENTATIVE, TRACK_REPORT_RESULT_STARTED, true);
    feed_measurement(&handler, 50, 1000);
    check_track_report(0, 50, 50, TRACK_STATE_TENTATIVE, TRACK_REPORT_RESULT_UPDATED, true);
    frame_handler_check_timeout(&handler, handler.radar.last_valid_rx_ms + CONFIG_RADAR_MEASUREMENT_TIMEOUT_MS);
    assert(handler.radar.track.state == TRACK_STATE_EMPTY && handler.report_sender.next_track_report_sequence == 1);
    feed_measurement(&handler, 150, 1000);
    check_track_report(1, 150, 150, TRACK_STATE_TENTATIVE, TRACK_REPORT_RESULT_STARTED, true);
}

static void test_track_submit_failure_and_recovery(void)
{
    const int errors[] = {-ENOMSG, -EINVAL, -EIO};
    for (size_t i = 0; i < 3; ++i) {
        struct frame_handler handler;
        reset_test(&handler);
        submit_result = errors[i];
        feed_measurement(&handler, 100, 1000);
        assert(submit_calls == 1 && handler.report_sender.next_track_report_sequence == 1);
        assert(handler.radar.track.state == TRACK_STATE_TENTATIVE);
        assert(strstr(output, "TRACK submit failed:") != NULL);
        assert(strstr(output, "TRACK report queued:") == NULL);
        assert((strstr(output, "TX queue full") != NULL) == (errors[i] == -ENOMSG));
        submit_result = 0;
        feed_measurement(&handler, 200, 1000);
        check_track_report(1, 200, 200, TRACK_STATE_TENTATIVE, TRACK_REPORT_RESULT_UPDATED, true);
        assert(submit_calls == 2 && handler.report_sender.next_track_report_sequence == 2);
        assert(strstr(output, "TRACK report queued:") != NULL);
    }
}

static void test_track_encode_failure_and_recovery(void)
{
    struct frame_handler handler;
    reset_test(&handler);
    track_encode_result = -EINVAL;
    feed_measurement(&handler, 100, 1000);
    assert(submit_calls == 0 && handler.report_sender.next_track_report_sequence == 1);
    assert(strstr(output, "TRACK encode failed:") != NULL);
    assert(strstr(output, "TRACK report queued:") == NULL);
    track_encode_result = 0;
    feed_measurement(&handler, 200, 1000);
    check_track_report(1, 200, 200, TRACK_STATE_TENTATIVE, TRACK_REPORT_RESULT_UPDATED, true);
    assert(submit_calls == 1);
}

static void test_incoming_track_report_is_ignored(void)
{
    struct frame_handler handler;
    reset_test(&handler);
    unsigned char before[sizeof(handler)];
    memcpy(before, &handler, sizeof(before));
    const struct rx_frame frame = {
        .frame = {.type = FRAME_TYPE_TRACK_REPORT, .payload_length = 32},
    };
    frame_handler_process(&handler, &frame);
    assert(submit_calls == 0 && stats_reads == 0);
    assert(memcmp(before, &handler, sizeof(before)) == 0);
    assert(strcmp(output, "TRACK_REPORT ignored: unexpected input\n") == 0);
}

static void test_processor_invalid_input_preserves_output(void)
{
    struct frame_handler handler;
    reset_test(&handler);
    struct radar_process_result result = {
        .input_timestamp_ms = 1234, .result = TRACK_REPORT_RESULT_UPDATED,
    };
    unsigned char before[sizeof(handler.radar)];
    memcpy(before, &handler.radar, sizeof(before));
    struct rx_frame frame = measurement(100, 0, 5000);
    assert(!radar_processor_process_measurement(&handler.radar, &frame.frame, frame.received_at_ms, &result));
    frame.frame.payload_length = 9;
    assert(!radar_processor_process_measurement(&handler.radar, &frame.frame, frame.received_at_ms, &result));
    assert(memcmp(before, &handler.radar, sizeof(before)) == 0);
    assert(result.input_timestamp_ms == 1234 && result.result == TRACK_REPORT_RESULT_UPDATED);
    assert(submit_calls == 0);
}

static void test_processor_timeout_event_once_without_transmission(void)
{
    struct frame_handler handler;
    reset_test(&handler);
    assert(!radar_processor_check_timeout(&handler.radar, 4000));
    const struct rx_frame frame = measurement(100, 1000, 5000);
    struct radar_process_result result;
    assert(radar_processor_process_measurement(&handler.radar, &frame.frame, frame.received_at_ms, &result));
    assert(result.input_timestamp_ms == 100 && result.result == TRACK_REPORT_RESULT_STARTED);
    assert(!radar_processor_check_timeout(&handler.radar, 5000 + CONFIG_RADAR_MEASUREMENT_TIMEOUT_MS - 1));
    assert(radar_processor_check_timeout(&handler.radar, 5000 + CONFIG_RADAR_MEASUREMENT_TIMEOUT_MS));
    const size_t log_size = output_size;
    assert(!radar_processor_check_timeout(&handler.radar, 5001 + CONFIG_RADAR_MEASUREMENT_TIMEOUT_MS));
    assert(output_size == log_size);
    assert(handler.radar.track.state == TRACK_STATE_EMPTY && !handler.radar.timeout_armed);
    assert(submit_calls == 0 && handler.report_sender.next_track_report_sequence == 0);
}

static const struct test_case cases[] = {
    TEST_CASE(test_processor_invalid_input_preserves_output),
    TEST_CASE(test_processor_timeout_event_once_without_transmission),
    TEST_CASE(test_track_encode_wire),
    TEST_CASE(test_track_encode_invalid_arguments),
    TEST_CASE(test_track_started_updated_and_confirmed),
    TEST_CASE(test_track_timestamp_rejection_report),
    TEST_CASE(test_track_gate_rejection_empty_and_lost),
    TEST_CASE(test_track_gap_reset_and_recovery),
    TEST_CASE(test_track_sequence_wrap_and_timeout_preservation),
    TEST_CASE(test_track_submit_failure_and_recovery),
    TEST_CASE(test_track_encode_failure_and_recovery),
    TEST_CASE(test_incoming_track_report_is_ignored),

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
