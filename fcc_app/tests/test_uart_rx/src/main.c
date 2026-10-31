#include <stdio.h>
// Include the real implementation to exercise its private recovery functions.
// Do not compile uart_rx.c separately into this test executable.
#include "uart_rx.c"

// Reserve one slot to distinguish a full ring from an empty ring.
#define RX_CAPACITY (BUFFER_SIZE - 1U)
_Static_assert(BUFFER_SIZE >= 9, "Overflow fixtures require at least eight usable slots");
_Static_assert(BUFFER_SIZE <= UINT8_MAX + 1U, "Ring indices must represent every slot");

static unsigned int handled;
static struct frame_parser last_frame;
static bool inject_during_handle;
static const uint8_t init_frame[] = {0x7e, 0x1, 0x1, 0x0, 0xc8, 0x9d};
static const uint8_t radar_frame[] = {
    0x7e, 0xa, 0x2, 0x0, 0x0, 0x0, 0x64, 0x3, 0xe8, 0xc0, 0x0, 0x0, 0x0, 0xf, 0xb1
};

static void receive(const uint8_t *bytes, size_t size)
{
    assert(!irq_locked);
    fifo_bytes = bytes; fifo_size = size; fifo_position = 0;
    uart_callback(&test_device, NULL);
    assert(fifo_position == size);
}

// Pad with bytes ignored in WAIT_STX and reject the byte after the usable capacity.
static void receive_overflow(const uint8_t *bytes, size_t size, size_t offset)
{
    uint8_t stream[BUFFER_SIZE] = {0};
    assert(offset + size <= RX_CAPACITY);
    memcpy(stream + offset, bytes, size);
    stream[RX_CAPACITY] = 0xaa;
    receive(stream, sizeof(stream));
    assert(atomic_get(&rx_overflow) == 1);
}

void frame_handler_init(struct frame_handler *h) { memset(h, 0, sizeof(*h)); }
void frame_handler_check_timeout(struct frame_handler *h, int64_t now)
{ (void)h; (void)now; assert(!irq_locked); }
void handle_frame(struct frame_handler *h, const struct frame_parser *p)
{
    (void)h;
    assert(!irq_locked);
    handled++;
    last_frame = *p;
    if (inject_during_handle) {
        assert(atomic_get(&rx_overflow) == 1);
        uint8_t head = rx_ring.head, tail = rx_ring.tail;
        receive(init_frame, sizeof(init_frame));
        assert(rx_ring.head == head && rx_ring.tail == tail);
        inject_during_handle = false;
    }
}

static void reset_test(void)
{
    irq_locked = 0;
    atomic_set(&rx_overflow, 0);
    handled = 0;
    inject_during_handle = false;
    ring_init(&rx_ring); parser_init(&parser);
}

static void assert_recovered(void)
{
    uint8_t byte;
    assert(!irq_locked);
    assert(!atomic_get(&rx_overflow));
    assert(!ring_pop(&rx_ring, &byte));
    assert(parser.state == WAIT_STX && parser.payload_index == 0);
}

static void test_dispatch_drains_complete_frame(void)
{
    reset_test();
    const uint8_t stream[] = {0x7e, 0x1, 0x1, 0x0, 0xc8, 0x9d, 0x7e, 0xa};
    receive_overflow(stream, sizeof(stream), RX_CAPACITY - sizeof(stream));
    rx_recover_overflow();
    assert(handled == 1);
    assert(last_frame.type == TYPE_INIT && last_frame.payload_length == 1);
    assert_recovered();
}

static void test_preserves_partial_parser_and_drops_new_input(void)
{
    reset_test();
    for (size_t i = 0; i < 7; ++i) assert(parser_feed(&parser, radar_frame[i]) == PARSER_INCOMPLETE);
    receive_overflow(radar_frame + 7, sizeof(radar_frame) - 7, 0);
    inject_during_handle = true;
    rx_recover_overflow();
    assert(handled == 1 && !inject_during_handle);
    assert(last_frame.type == TYPE_RADAR_MEAS && last_frame.payload_length == 10);
    assert(memcmp(last_frame.payload, radar_frame + 3, 10) == 0);
    assert_recovered();
}

static void test_discards_incomplete_frame(void)
{
    reset_test();
    // Leave only a prefix at the end of the buffer so padding cannot complete it.
    receive_overflow(radar_frame, 7, RX_CAPACITY - 7);
    rx_recover_overflow();
    assert(handled == 0);
    assert_recovered();
}

static void test_rejects_bad_crc(void)
{
    reset_test();
    const uint8_t stream[] = {0x7e,1,1,0,0xff,0xff};
    receive_overflow(stream, sizeof(stream), 0);
    rx_recover_overflow();
    assert(handled == 0);
    assert_recovered();
}

static void test_drain_rejects_bad_length_then_handles_valid_frame(void)
{
    reset_test();
    uint8_t stream[2 + sizeof(init_frame)] = {0x7e, 0};
    memcpy(stream + 2, init_frame, sizeof(init_frame));
    receive_overflow(stream, sizeof(stream), 0);
    rx_recover_overflow();
    assert(handled == 1 && last_frame.type == TYPE_INIT);
    assert_recovered();
}

static void test_receives_after_recovery(void)
{
    test_discards_incomplete_frame();
    receive(init_frame, sizeof(init_frame));
    assert(!atomic_get(&rx_overflow));
    uint8_t byte;
    while (ring_pop(&rx_ring, &byte)) {
        if (parser_feed(&parser, byte) == PARSER_FRAME_READY) handle_frame(&handler, &parser);
    }
    assert(handled == 1 && last_frame.type == TYPE_INIT);
}

static void test_empty_recovery(void)
{
    reset_test(); atomic_set(&rx_overflow, 1);
    rx_recover_overflow();
    assert(handled == 0);
    assert_recovered();
}

static void test_ring_overflow_preserves_buffer(void)
{
    reset_test();
    for (unsigned int i=0; i<BUFFER_SIZE-1; ++i) assert(ring_push(&rx_ring, i));
    uint8_t head = rx_ring.head, tail = rx_ring.tail;
    assert(!ring_push(&rx_ring, 0xff));
    assert(rx_ring.head == head && rx_ring.tail == tail);
    for (unsigned int i=0; i<BUFFER_SIZE-1; ++i) {
        uint8_t byte; assert(ring_pop(&rx_ring, &byte)); assert(byte == i);
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

struct test_case {
    const char *name;
    void (*run)(void);
};

#define CASE(test) { #test, test }
static const struct test_case cases[] = {
    CASE(test_dispatch_drains_complete_frame),
    CASE(test_preserves_partial_parser_and_drops_new_input),
    CASE(test_discards_incomplete_frame),
    CASE(test_rejects_bad_crc),
    CASE(test_drain_rejects_bad_length_then_handles_valid_frame),
    CASE(test_receives_after_recovery),
    CASE(test_empty_recovery),
    CASE(test_ring_overflow_preserves_buffer),
    CASE(test_ring_wraparound),
};

int main(int argc, char **argv)
{
    // The infinite RX thread is compiled but not scheduled by this host harness.
    (void)uart_rx_thread;
    bool matched = false;
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        if (argc == 1 || (argc == 2 && strcmp(argv[1], cases[i].name) == 0)) {
            cases[i].run();
            printf("PASS: %s\n", cases[i].name);
            matched = true;
        }
    }
    if (!matched) {
        fprintf(stderr, "Usage: %s [test_name]\n", argv[0]);
        return 2;
    }
    return 0;
}
