#include "transport/uart_tx.c"
#include "protocol/frame_parser.h"

static void reset_fixture(void)
{
    ready = true;
    tx_ready_sem.count = 0;
    tx_queue.head = tx_queue.count = 0;
    put_calls = 0;
    sent_count = 0;
    memset(log_output, 0, sizeof(log_output));
}

static void run_worker(void)
{
    assert(tx_ready_sem.count == 1);
    if (setjmp(worker_exit) == 0) uart_tx_thread(NULL, NULL, NULL);
}

static void test_tx_init(void)
{
    reset_fixture();
    ready = false;
    assert(uart_tx_init() != 0 && tx_ready_sem.count == 0);
    assert(strstr(log_output, "not ready") != NULL);
    ready = true;
    assert(uart_tx_init() == 0 && tx_ready_sem.count == 1);
}

static void test_tx_invalid_submit(void)
{
    reset_fixture();
    struct send_frame request = {.frame = {.type = FRAME_TYPE_STATUS}};
    assert(uart_tx_submit(NULL) == -EINVAL);
    const uint8_t lengths[] = {0, MAX_PAYLOAD + 1, UINT8_MAX};
    for (size_t i = 0; i < sizeof(lengths); ++i) {
        request.frame.payload_length = lengths[i];
        assert(uart_tx_submit(&request) == -EINVAL);
    }
    assert(put_calls == 0 && tx_queue.count == 0 && sent_count == 0);
}

static void test_tx_queue_copy_fifo_full_and_recovery(void)
{
    reset_fixture();
    assert(uart_tx_init() == 0);
    struct send_frame request = {
        .frame = {.type = FRAME_TYPE_STATUS, .payload_length = 1},
    };
    for (unsigned i = 0; i < 8; ++i) {
        request.frame.payload[0] = (uint8_t)i;
        assert(uart_tx_submit(&request) == 0);
    }
    assert(uart_tx_submit(&request) == -ENOMSG);
    memset(&request, 0xff, sizeof(request));
    run_worker();
    assert(sent_count == 8 * 6 && tx_queue.count == 0);
    struct frame_parser parser;
    parser_init(&parser);
    unsigned frames = 0;
    for (size_t i = 0; i < sent_count; ++i) {
        ParserResult result = parser_feed(&parser, sent[i]);
        if (result == PARSER_RESULT_FRAME_READY) {
            assert(parser.frame.type == FRAME_TYPE_STATUS && parser.frame.payload_length == 1);
            assert(parser.frame.payload[0] == frames++);
        } else {
            assert(result == PARSER_RESULT_INCOMPLETE);
        }
    }
    assert(frames == 8);
    request = (struct send_frame){.frame = {.type = FRAME_TYPE_INIT, .payload_length = 1}};
    assert(uart_tx_submit(&request) == 0);
    assert(uart_tx_init() == 0);
    sent_count = 0;
    run_worker();
    const uint8_t expected[] = {0x7e, 1, 1, 0, 0xc8, 0x9d};
    assert(sent_count == sizeof(expected) && memcmp(sent, expected, sizeof(expected)) == 0);
}

static void test_tx_encode_error_continues(void)
{
    reset_fixture();
    assert(uart_tx_init() == 0);
    struct send_frame request = {.frame = {.type = FRAME_TYPE_INIT, .payload_length = 0}};
    // Bypass submit validation solely to exercise the worker's defensive path.
    assert(k_msgq_put(&tx_queue, &request, K_NO_WAIT) == 0);
    request.frame.payload_length = 1;
    assert(uart_tx_submit(&request) == 0);
    run_worker();
    assert(strstr(log_output, "TX encode failed:") != NULL);
    const uint8_t expected[] = {0x7e, 1, 1, 0, 0xc8, 0x9d};
    assert(sent_count == sizeof(expected) && memcmp(sent, expected, sizeof(expected)) == 0);
}

int main(int argc, char **p_argv)
{
    assert(argc == 2);
#define RUN(test) if (strcmp(p_argv[1], #test) == 0) { test(); return 0; }
    RUN(test_tx_init)
    RUN(test_tx_invalid_submit)
    RUN(test_tx_queue_copy_fifo_full_and_recovery)
    RUN(test_tx_encode_error_continues)
    return 2;
}
