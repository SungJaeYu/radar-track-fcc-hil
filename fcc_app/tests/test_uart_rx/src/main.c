#include "../../common/test_runner.h"

#include <stdio.h>
#include <errno.h>
#include <string.h>
// Include the real implementation to exercise its private recovery functions.
// Do not compile uart_rx.c separately into this test executable.
#include "transport/uart_rx.c"
#include "transport/rx_frame_processor.c"

// Reserve one slot to distinguish a full ring from an empty ring.
#define RX_CAPACITY (RING_BUFFER_SIZE - 1U)
_Static_assert(RING_BUFFER_SIZE >= 9, "Overflow fixtures require at least eight usable slots");
_Static_assert(RING_BUFFER_SIZE <= UINT8_MAX + 1U, "Ring indices must represent every slot");

static unsigned int submitted;
static struct received_frame last_frame;
static int submit_result;
static unsigned int submit_attempts;
static bool inject_during_submit;
static const uint8_t init_frame[] = {0x7e, 0x1, 0x1, 0x0, 0xc8, 0x9d};
static const uint8_t radar_frame[] = {0x7e, 0xa,  0x2, 0x0, 0x0, 0x0, 0x64, 0x3,
                                      0xe8, 0xc0, 0x0, 0x0, 0x0, 0xf, 0xb1};

static void receive(const uint8_t *p_bytes, size_t size)
{
    assert(!irq_locked);
    p_fifo_bytes = p_bytes;
    fifo_size = size;
    fifo_position = 0;
    uart_callback(&test_device, NULL);
    assert(fifo_position == size);
}

// Pad with bytes ignored in PARSER_STATE_WAIT_STX and reject the byte after the usable capacity.
static void receive_overflow(const uint8_t *p_bytes, size_t size, size_t offset)
{
    uint8_t stream[RING_BUFFER_SIZE] = {0};
    assert(offset + size <= RX_CAPACITY);
    memcpy(stream + offset, p_bytes, size);
    stream[RX_CAPACITY] = 0xaa;
    receive(stream, sizeof(stream));
    assert(atomic_get(&rx_overflow) == 1);
}

int frame_handler_submit(const struct received_frame *p_frame)
{
    assert(!irq_locked);
    submit_attempts++;
    if (submit_result != 0) {
        return submit_result;
    }
    submitted++;
    last_frame = *p_frame;
    if (inject_during_submit) {
        assert(atomic_get(&rx_overflow) == 1);
        uint8_t head = rx_ring.head, tail = rx_ring.tail;
        receive(init_frame, sizeof(init_frame));
        assert(rx_ring.head == head && rx_ring.tail == tail);
        inject_during_submit = false;
    }
    return 0;
}

static void reset_test(void)
{
    irq_locked = 0;
    atomic_set(&rx_overflow, 0);
    submitted = 0;
    submit_attempts = 0;
    submit_result = 0;
    atomic_set(&queue_drops_count, 0);
    atomic_set(&frame_timeout_count, 0);
    atomic_set(&crc_error_count, 0);
    atomic_set(&length_error_count, 0);
    last_byte_processed_ms = 0;
    test_now_ms = 100;
    memset(&last_frame, 0, sizeof(last_frame));
    inject_during_submit = false;
    ring_init(&rx_ring);
    rx_frame_processor_reset();
}

static void assert_recovered(void)
{
    uint8_t byte;
    assert(!irq_locked);
    assert(!atomic_get(&rx_overflow));
    assert(!ring_pop(&rx_ring, &byte));
    assert(parser.state == PARSER_STATE_WAIT_STX && parser.payload_index == 0);
}

#if defined(CONFIG_RADAR_RX_OVERFLOW_DRAIN)
static void test_dispatch_drains_complete_frame(void)
{
    reset_test();
    const uint8_t stream[] = {0x7e, 0x1, 0x1, 0x0, 0xc8, 0x9d, 0x7e, 0xa};
    receive_overflow(stream, sizeof(stream), RX_CAPACITY - sizeof(stream));
    rx_recover_overflow();
    assert(submitted == 1);
    assert(last_frame.frame.type == FRAME_TYPE_INIT && last_frame.frame.payload_length == 1);
    assert_recovered();
}
#endif

#if defined(CONFIG_RADAR_RX_OVERFLOW_DRAIN)
static void test_preserves_partial_parser_and_drops_new_input(void)
{
    reset_test();
    for (size_t i = 0; i < 7; ++i) {
        rx_frame_processor_feed(radar_frame[i]);
    }
    assert(submit_attempts == 0);
    receive_overflow(radar_frame + 7, sizeof(radar_frame) - 7, 0);
    inject_during_submit = true;
    rx_recover_overflow();
    assert(submitted == 1 && !inject_during_submit);
    assert(last_frame.frame.type == FRAME_TYPE_MEAS_REPORT &&
           last_frame.frame.payload_length == 10);
    assert(memcmp(last_frame.frame.payload, radar_frame + 3, 10) == 0);
    assert_recovered();
}
#endif

static void test_discards_incomplete_frame(void)
{
    reset_test();
    // Leave only a prefix at the end of the buffer so padding cannot complete it.
    receive_overflow(radar_frame, 7, RX_CAPACITY - 7);
    rx_recover_overflow();
    assert(submitted == 0);
    assert_recovered();
}

#if defined(CONFIG_RADAR_RX_OVERFLOW_DRAIN)
static void test_rejects_bad_crc(void)
{
    reset_test();
    const uint8_t stream[] = {0x7e, 1, 1, 0, 0xff, 0xff};
    receive_overflow(stream, sizeof(stream), 0);
    rx_recover_overflow();
    assert(submitted == 0);
    assert_recovered();
}
#endif

#if defined(CONFIG_RADAR_RX_OVERFLOW_DRAIN)
static void test_drain_rejects_bad_length_then_handles_valid_frame(void)
{
    reset_test();
    uint8_t stream[2 + sizeof(init_frame)] = {0x7e, 0};
    memcpy(stream + 2, init_frame, sizeof(init_frame));
    receive_overflow(stream, sizeof(stream), 0);
    rx_recover_overflow();
    assert(submitted == 1 && last_frame.frame.type == FRAME_TYPE_INIT);
    assert_recovered();
}
#endif

static void test_receives_after_recovery(void)
{
    test_discards_incomplete_frame();
    receive(init_frame, sizeof(init_frame));
    assert(!atomic_get(&rx_overflow));
    uint8_t byte;
    while (ring_pop(&rx_ring, &byte)) {
        rx_frame_processor_feed(byte);
    }
    assert(submitted == 1 && last_frame.frame.type == FRAME_TYPE_INIT);
}

static void test_empty_recovery(void)
{
    reset_test();
    atomic_set(&rx_overflow, 1);
    rx_recover_overflow();
    assert(submitted == 0);
    assert_recovered();
}

static void test_ring_overflow_preserves_buffer(void)
{
    reset_test();
    for (unsigned int i = 0; i < RING_BUFFER_SIZE - 1; ++i) {
        assert(ring_push(&rx_ring, i));
    }
    uint8_t head = rx_ring.head, tail = rx_ring.tail;
    assert(!ring_push(&rx_ring, 0xff));
    assert(rx_ring.head == head && rx_ring.tail == tail);
    for (unsigned int i = 0; i < RING_BUFFER_SIZE - 1; ++i) {
        uint8_t byte;
        assert(ring_pop(&rx_ring, &byte));
        assert(byte == i);
    }
}

static void test_ring_wraparound(void)
{
    reset_test();
    // Repeated full/empty cycles cross the array boundary without resetting indices.
    for (unsigned int round = 0; round < 3; ++round) {
        for (unsigned int i = 0; i < RX_CAPACITY; ++i) {
            assert(ring_push(&rx_ring, (uint8_t)(i + round)));
        }
        assert(!ring_push(&rx_ring, 0xff));
        for (unsigned int i = 0; i < RX_CAPACITY; ++i) {
            uint8_t byte;
            assert(ring_pop(&rx_ring, &byte));
            assert(byte == (uint8_t)(i + round));
        }
        uint8_t byte;
        assert(!ring_pop(&rx_ring, &byte));
    }
}

static void test_processor_copies_frame_and_reset_preserves_drops(void)
{
    reset_test();
    for (size_t i = 0; i < sizeof(radar_frame); ++i) {
        rx_frame_processor_feed(radar_frame[i]);
    }
    assert(submitted == 1 && submit_attempts == 1);
    assert(last_frame.frame.type == FRAME_TYPE_MEAS_REPORT &&
           last_frame.frame.payload_length == 10);
    assert(memcmp(last_frame.frame.payload, radar_frame + 3, 10) == 0);
    assert(last_frame.received_at_ms == k_uptime_get());
    assert(queue_drops_count == 0);
    memset(parser.frame.payload, 0, sizeof(parser.frame.payload));
    queue_drops_count = 3;
    rx_frame_processor_reset();
    assert(queue_drops_count == 3 && parser.state == PARSER_STATE_WAIT_STX);
    assert(memcmp(last_frame.frame.payload, radar_frame + 3, 10) == 0);
}

static void test_nonready_results_do_not_submit(void)
{
    reset_test();
    process_parser_result(PARSER_RESULT_INCOMPLETE);
    process_parser_result(PARSER_RESULT_CRC_ERROR);
    process_parser_result(PARSER_RESULT_LENGTH_ERROR);
    assert(submit_attempts == 0 && queue_drops_count == 0);
}

static void test_queue_full_counts_drop_and_recovers(void)
{
    reset_test();
    submit_result = -ENOMSG;
    for (size_t i = 0; i < sizeof(init_frame); ++i) {
        rx_frame_processor_feed(init_frame[i]);
    }
    assert(submit_attempts == 1 && submitted == 0 && queue_drops_count == 1);
    submit_result = 0;
    for (size_t i = 0; i < sizeof(radar_frame); ++i) {
        rx_frame_processor_feed(radar_frame[i]);
    }
    assert(submit_attempts == 2 && submitted == 1 && queue_drops_count == 1);
    assert(last_frame.frame.type == FRAME_TYPE_MEAS_REPORT);
}

#if defined(CONFIG_RADAR_RX_OVERFLOW_DRAIN)
static void test_drain_queue_full_still_finishes_recovery(void)
{
    reset_test();
    submit_result = -ENOMSG;
    receive_overflow(init_frame, sizeof(init_frame), 0);
    rx_recover_overflow();
    assert(submit_attempts == 1 && submitted == 0 && queue_drops_count == 1);
    assert_recovered();
}
#endif

static void feed_bytes(const uint8_t *p_bytes, size_t size)
{
    for (size_t i = 0; i < size; ++i) {
        rx_frame_processor_feed(p_bytes[i]);
    }
}

static void test_timeout_boundary_for_every_partial_frame(void)
{
    // Exercise every cut, including header, payload and a missing CRC low byte.
    for (size_t cut = 1; cut < sizeof(radar_frame); ++cut) {
        reset_test();
        feed_bytes(radar_frame, cut);
        ParserState pending_state = parser.state;
        assert(pending_state != PARSER_STATE_WAIT_STX);
        test_now_ms += FRAME_RX_TIMEOUT_MS - 1;
        rx_frame_processor_check_timeout(test_now_ms);
        assert(parser.state == pending_state && frame_timeout_count == 0);
        test_now_ms++;
        rx_frame_processor_check_timeout(test_now_ms);
        assert(parser.state == PARSER_STATE_WAIT_STX && parser.payload_index == 0);
        assert(frame_timeout_count == 1 && submit_attempts == 0);
        test_now_ms += FRAME_RX_TIMEOUT_MS * 2;
        rx_frame_processor_check_timeout(test_now_ms);
        assert(frame_timeout_count == 1);
    }
}

static void test_timeout_ignores_idle_and_complete_frames(void)
{
    reset_test();
    test_now_ms += FRAME_RX_TIMEOUT_MS * 2;
    rx_frame_processor_check_timeout(test_now_ms);
    assert(frame_timeout_count == 0);
    feed_bytes(radar_frame, sizeof(radar_frame));
    assert(submitted == 1);
    test_now_ms += FRAME_RX_TIMEOUT_MS * 2;
    rx_frame_processor_check_timeout(test_now_ms);
    assert(frame_timeout_count == 0 && submitted == 1);
}

static void test_timeout_recovers_next_valid_frame(void)
{
    reset_test();
    feed_bytes(radar_frame, 7);
    test_now_ms += FRAME_RX_TIMEOUT_MS;
    rx_frame_processor_check_timeout(test_now_ms);
    assert(frame_timeout_count == 1 && submit_attempts == 0);
    feed_bytes(radar_frame, sizeof(radar_frame));
    assert(submitted == 1 && last_frame.frame.type == FRAME_TYPE_MEAS_REPORT);
    assert(last_frame.frame.payload_length == 10);
    assert(memcmp(last_frame.frame.payload, radar_frame + 3, 10) == 0);
    assert(last_frame.received_at_ms == test_now_ms);
    assert(frame_timeout_count == 1 && queue_drops_count == 0);
}

static void test_timeout_preserves_split_frames(void)
{
    for (size_t cut = 1; cut < sizeof(radar_frame); ++cut) {
        reset_test();
        feed_bytes(radar_frame, cut);
        test_now_ms += FRAME_RX_TIMEOUT_MS - 1;
        rx_frame_processor_check_timeout(test_now_ms);
        feed_bytes(radar_frame + cut, sizeof(radar_frame) - cut);
        assert(submitted == 1 && frame_timeout_count == 0);
        assert(last_frame.frame.payload_length == 10);
        assert(memcmp(last_frame.frame.payload, radar_frame + 3, 10) == 0);
    }
}

static void test_timeout_refreshes_on_each_byte(void)
{
    reset_test();
    int64_t started_ms = test_now_ms;
    // Total assembly exceeds the threshold, but each gap stays below it.
    for (size_t i = 0; i < sizeof(radar_frame); ++i) {
        rx_frame_processor_feed(radar_frame[i]);
        test_now_ms += FRAME_RX_TIMEOUT_MS - 1;
        rx_frame_processor_check_timeout(test_now_ms);
        assert(frame_timeout_count == 0);
    }
    assert(test_now_ms - started_ms > FRAME_RX_TIMEOUT_MS);
    assert(submitted == 1 && last_frame.frame.type == FRAME_TYPE_MEAS_REPORT);
}

static void test_explicit_reset_cancels_partial_timeout(void)
{
    reset_test();
    feed_bytes(radar_frame, 7);
    test_now_ms += FRAME_RX_TIMEOUT_MS;
    rx_frame_processor_check_timeout(test_now_ms);
    assert(frame_timeout_count == 1);
    feed_bytes(radar_frame, 7);
    rx_frame_processor_reset();
    test_now_ms += FRAME_RX_TIMEOUT_MS;
    rx_frame_processor_check_timeout(test_now_ms);
    assert(frame_timeout_count == 1 && parser.state == PARSER_STATE_WAIT_STX);
    assert(submit_attempts == 0);
}

static void test_stats_snapshot_and_reset(void)
{
    reset_test();
    struct rx_frame_stats snapshot;
    rx_frame_processor_get_stats(&snapshot);
    assert(snapshot.crc_errors == 0 && snapshot.length_errors == 0);
    assert(snapshot.frame_timeouts == 0 && snapshot.queue_drops == 0);

    uint8_t bad_crc[sizeof(radar_frame)];
    memcpy(bad_crc, radar_frame, sizeof(bad_crc));
    bad_crc[sizeof(bad_crc) - 1] ^= 1;
    feed_bytes(bad_crc, sizeof(bad_crc));
    const uint8_t bad_length[] = {0x7e, 0};
    feed_bytes(bad_length, sizeof(bad_length));
    feed_bytes(radar_frame, 7);
    test_now_ms += FRAME_RX_TIMEOUT_MS;
    rx_frame_processor_check_timeout(test_now_ms);
    submit_result = -ENOMSG;
    feed_bytes(radar_frame, sizeof(radar_frame));
    rx_frame_processor_reset();

    rx_frame_processor_get_stats(&snapshot);
    assert(snapshot.crc_errors == 1 && snapshot.length_errors == 1);
    assert(snapshot.frame_timeouts == 1 && snapshot.queue_drops == 1);
    submit_result = 0;
    feed_bytes(radar_frame, sizeof(radar_frame));
    struct rx_frame_stats after;
    rx_frame_processor_get_stats(&after);
    assert(after.crc_errors == snapshot.crc_errors);
    assert(after.length_errors == snapshot.length_errors);
    assert(after.frame_timeouts == snapshot.frame_timeouts);
    assert(after.queue_drops == snapshot.queue_drops);
    assert(submitted == 1);
}

static void test_rx_init_errors(void)
{
    reset_test();
    rx_ready_sem.count = 0;
    test_callback_calls = 0;
    test_rx_enable_calls = 0;
    test_device_ready = false;
    assert(uart_rx_init() == -ENODEV);
    assert(test_callback_calls == 0 && test_rx_enable_calls == 0 && rx_ready_sem.count == 0);

    test_device_ready = true;
    const int errors[] = {-ENOSYS, -ENOTSUP};
    for (size_t i = 0; i < sizeof(errors) / sizeof(errors[0]); ++i) {
        test_callback_result = errors[i];
        assert(uart_rx_init() == errors[i]);
        assert(test_rx_enable_calls == 0 && rx_ready_sem.count == 0);
    }

    test_callback_result = 0;
    assert(uart_rx_init() == 0);
    assert(test_callback_calls == 3 && test_rx_enable_calls == 1 && rx_ready_sem.count == 1);
}

#if defined(CONFIG_RADAR_RX_OVERFLOW_DISCARD)
static void test_discard_drops_complete_frame_and_recovers(void)
{
    reset_test();
    receive_overflow(init_frame, sizeof(init_frame), 0);
    // New ISR input must be ignored while overflow recovery is pending.
    const uint8_t head = rx_ring.head;
    const uint8_t tail = rx_ring.tail;
    receive(init_frame, sizeof(init_frame));
    assert(rx_ring.head == head && rx_ring.tail == tail);
    queue_drops_count = 3;
    crc_error_count = 4;
    length_error_count = 5;
    frame_timeout_count = 6;

    rx_recover_overflow();

    assert(submit_attempts == 0 && submitted == 0);
    assert(queue_drops_count == 3 && crc_error_count == 4);
    assert(length_error_count == 5 && frame_timeout_count == 6);
    assert_recovered();
    receive(init_frame, sizeof(init_frame));
    uint8_t byte;
    while (ring_pop(&rx_ring, &byte)) {
        rx_frame_processor_feed(byte);
    }
    assert(submitted == 1 && last_frame.frame.type == FRAME_TYPE_INIT);
}

static void test_discard_drops_completable_partial_frame(void)
{
    reset_test();
    for (size_t i = 0; i < 7; ++i) {
        rx_frame_processor_feed(radar_frame[i]);
    }
    receive_overflow(radar_frame + 7, sizeof(radar_frame) - 7, 0);

    rx_recover_overflow();

    assert(submit_attempts == 0 && submitted == 0);
    assert_recovered();
    // Discarded fragments must not contribute to the next frame.
    for (size_t i = 0; i < sizeof(radar_frame); ++i) {
        rx_frame_processor_feed(radar_frame[i]);
    }
    assert(submitted == 1 && last_frame.frame.type == FRAME_TYPE_MEAS_REPORT);
    assert(memcmp(last_frame.frame.payload, radar_frame + 3, 10) == 0);
}
#endif

static const struct test_case cases[] = {
#if defined(CONFIG_RADAR_RX_OVERFLOW_DISCARD)
    TEST_CASE(test_discard_drops_complete_frame_and_recovers),
    TEST_CASE(test_discard_drops_completable_partial_frame),
#endif
    TEST_CASE(test_rx_init_errors),
    TEST_CASE(test_stats_snapshot_and_reset),
    TEST_CASE(test_timeout_boundary_for_every_partial_frame),
    TEST_CASE(test_timeout_ignores_idle_and_complete_frames),
    TEST_CASE(test_timeout_recovers_next_valid_frame),
    TEST_CASE(test_timeout_preserves_split_frames),
    TEST_CASE(test_timeout_refreshes_on_each_byte),
    TEST_CASE(test_explicit_reset_cancels_partial_timeout),

    TEST_CASE(test_processor_copies_frame_and_reset_preserves_drops),
    TEST_CASE(test_nonready_results_do_not_submit),
    TEST_CASE(test_queue_full_counts_drop_and_recovers),
#if defined(CONFIG_RADAR_RX_OVERFLOW_DRAIN)
    TEST_CASE(test_drain_queue_full_still_finishes_recovery),
#endif

#if defined(CONFIG_RADAR_RX_OVERFLOW_DRAIN)
    TEST_CASE(test_dispatch_drains_complete_frame),
#endif
#if defined(CONFIG_RADAR_RX_OVERFLOW_DRAIN)
    TEST_CASE(test_preserves_partial_parser_and_drops_new_input),
#endif
    TEST_CASE(test_discards_incomplete_frame),
#if defined(CONFIG_RADAR_RX_OVERFLOW_DRAIN)
    TEST_CASE(test_rejects_bad_crc),
#endif
#if defined(CONFIG_RADAR_RX_OVERFLOW_DRAIN)
    TEST_CASE(test_drain_rejects_bad_length_then_handles_valid_frame),
#endif
    TEST_CASE(test_receives_after_recovery),
    TEST_CASE(test_empty_recovery),
    TEST_CASE(test_ring_overflow_preserves_buffer),
    TEST_CASE(test_ring_wraparound),
};

int main(int argc, char **p_argv)
{
    // The worker is compiled but is not scheduled by the host test.
    (void)uart_rx_thread;
    return run_tests(argc, p_argv, cases, sizeof(cases) / sizeof(cases[0]));
}
