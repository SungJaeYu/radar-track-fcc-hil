#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/atomic.h>

#include "rx_frame_processor.h"
#include "frame_parser.h"
#include "frame_handler.h"

#define FRAME_RX_TIMEOUT_MS 1000

// Accessed only by the RX thread after initialization
static struct frame_parser parser;
static int64_t last_byte_processed_ms;

static atomic_t crc_error_count = ATOMIC_INIT(0);
static atomic_t length_error_count = ATOMIC_INIT(0);
static atomic_t frame_timeout_count = ATOMIC_INIT(0);
static atomic_t queue_drops_count = ATOMIC_INIT(0);

static void process_parser_result(ParserResult result);
static void submit_parsed_frame(void);


void rx_frame_processor_reset(void)
{
    parser_init(&parser);
}

void rx_frame_processor_feed(uint8_t byte)
{
    last_byte_processed_ms = k_uptime_get();

    ParserResult result = parser_feed(&parser, byte);

    process_parser_result(result);
}

void rx_frame_processor_check_timeout(int64_t now_ms)
{
    if (parser.state == WAIT_STX) {
        return;
    }
    
    if (now_ms - last_byte_processed_ms < FRAME_RX_TIMEOUT_MS) {
        return;
    }

    rx_frame_processor_reset();
    atomic_inc(&frame_timeout_count);
}

void rx_frame_processor_get_stats(struct rx_frame_stats *p_stats)
{
    p_stats->crc_errors = atomic_get(&crc_error_count);
    p_stats->length_errors = atomic_get(&length_error_count);
    p_stats->frame_timeouts = atomic_get(&frame_timeout_count);
    p_stats->queue_drops = atomic_get(&queue_drops_count);
}

static void process_parser_result(ParserResult result)
{
    switch(result) {
        case PARSER_FRAME_READY:
            submit_parsed_frame();
            break;
        case PARSER_CRC_ERROR:
            atomic_inc(&crc_error_count);
            break;
        case PARSER_LENGTH_ERROR:
            atomic_inc(&length_error_count);
            break;
        case PARSER_INCOMPLETE:
        default:
            // Do nothing
            break;
    }
}

static void submit_parsed_frame(void)
{
    struct received_frame received = {
        .type = parser.type,
        .payload_length = parser.payload_length,
        .received_at_ms = k_uptime_get(),
    };
    memcpy(received.payload, parser.payload, parser.payload_length);

    int ret = frame_handler_submit(&received);

    if (ret == -ENOMSG) {
        // Count the new frame dropped because the queue is full.
        atomic_inc(&queue_drops_count);
        if (atomic_get(&queue_drops_count) == 1 || atomic_get(&queue_drops_count) % 100 == 0) {
            printk("Frame queue drops: %u\n", (unsigned int)atomic_get(&queue_drops_count));
        }
    } else if (ret == -EINVAL) {
        // TODO: Invalid Value
    }
}