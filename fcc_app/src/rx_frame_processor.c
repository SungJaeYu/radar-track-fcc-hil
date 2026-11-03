#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "rx_frame_processor.h"
#include "frame_parser.h"
#include "frame_handler.h"

#define FRAME_RX_TIMEOUT_MS 1000

// Accessed only by the RX thread after initialization
static struct frame_parser parser;
static uint32_t frame_queue_drop_count;
static int64_t last_byte_processed_ms;
static uint32_t frame_timeout_count;

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
    frame_timeout_count++;
}

static void process_parser_result(ParserResult result)
{
    switch(result) {
        case PARSER_FRAME_READY:
            submit_parsed_frame();
            break;
        case PARSER_CRC_ERROR:
        case PARSER_LENGTH_ERROR:
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

    if (ret != 0) {
        // Count the new frame dropped because the queue is full.
        frame_queue_drop_count++;

        if (frame_queue_drop_count == 1 || frame_queue_drop_count % 100 == 0) {
            printk("Frame queue drops: %u\n", (unsigned int)frame_queue_drop_count);
        }
    }
}