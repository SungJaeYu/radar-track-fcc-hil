#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/atomic.h>

#include <string.h>

#include "ring_buffer.h"
#include "frame_parser.h"
#include "uart_rx.h"
#include "frame_handler.h"

// Set UART device
static const struct device *p_uart = DEVICE_DT_GET(DT_NODELABEL(usart6));

static struct ring_buffer rx_ring;
static struct frame_parser parser;

static uint32_t frame_queue_drop_count;

// 1 means bytes were lost: the ISR drops new input until the RX thread finishes recovery.
static atomic_t rx_overflow = ATOMIC_INIT(0);

// Notify the RX thread of buffered data or overflow; actual bytes remain in rx_ring.
K_SEM_DEFINE(rx_sem, 0, 1);
// Keep the RX thread waiting until the ring, parser, and handler are initialized.
K_SEM_DEFINE(rx_ready_sem, 0, 1);

static void uart_callback(const struct device *p_dev, void *p_user_data);
static void uart_rx_thread(void *p_arg1, void *p_arg2, void *p_arg3);

static void rx_recover_discard_all(void);
static void rx_recover_drain_buffer(void);
static void rx_recover_overflow(void);
static void rx_finish_recovery(void);

static void process_parser_result(ParserResult result, const struct frame_parser *p_parser);
static void submit_parsed_frame(const struct frame_parser *p_parser);

K_THREAD_DEFINE(uart_rx_tid, 1024, uart_rx_thread, NULL, NULL, NULL, 5, 0, 0);

int uart_rx_init(void) {
    // Check UART device is ready
    if (!device_is_ready(p_uart)) {
        // Error Handling
        printk("USART6 is not ready\n");
        return -1;
    }

    // Set Callback Function
    if(uart_irq_callback_user_data_set(p_uart, uart_callback, NULL) != 0) {
        // error
        printk("Failed to set UART callback\n");
        return -1;
    }

    ring_init(&rx_ring);

    // init Parser
    parser_init(&parser);


    // Enable RX Interrupt
    uart_irq_rx_enable(p_uart);

    // Signal that RX initialization is completed
    k_sem_give(&rx_ready_sem);

    return 0;
}

static void uart_callback(const struct device *p_dev, void *p_user_data) {
    // Avoid Warning
    ARG_UNUSED(p_user_data);

    // UART Iterrupt Update
    uart_irq_update(p_dev);

    // UART Interrupt is pending
    while (uart_irq_is_pending(p_dev)) {
        // if UART Interrupt Receive Ready
        if (uart_irq_rx_ready(p_dev)) {
            uint8_t c;
            // UART FIFO Read
            while(uart_fifo_read(p_dev, &c, 1) == 1) {
                // Keep draining the hardware FIFO during recovery, but discard new bytes.
                // Drain recovery requires the buffer to contain only bytes received before the first loss.
                if (atomic_get(&rx_overflow) != 0) {
                    continue;
                }

                if (!ring_push(&rx_ring, c)) {
                    // Drop the byte that could not be stored and mark recovery as required.
                    atomic_set(&rx_overflow, 1);
                }
                // Wake the thread on successful storage or the first overflow.
                k_sem_give(&rx_sem);
            }
        }
    }
}

static void uart_rx_thread(void *p_arg1, void *p_arg2, void *p_arg3)
{
    ARG_UNUSED(p_arg1);
    ARG_UNUSED(p_arg2);
    ARG_UNUSED(p_arg3);

    // Wait for RX initialization to complete
    k_sem_take(&rx_ready_sem, K_FOREVER);

    while (1) {
        // Wake on RX notification, or after 50 ms even when no data arrives.
        // This wait interval is separate from the measurement timeout threshold.
        k_sem_take(&rx_sem, K_MSEC(50));

        while (1) {
            uint8_t c;

            // Prevent the UART ISR from interrupting the overflow check and single-byte parsing.
            unsigned int key = irq_lock();

            if (atomic_get(&rx_overflow) != 0) {
                // Allow interrupts during tracking and logging in drain recovery.
                // The flag remains set, so the ISR still cannot write to the ring buffer.
                irq_unlock(key);

                rx_recover_overflow();

                printk("RX overflow: recovery completed\n");

                break;
            }

            if (!ring_pop(&rx_ring, &c)) {
                irq_unlock(key);
                break;
            }

            ParserResult parser_result = parser_feed(&parser, c);

            irq_unlock(key);

            process_parser_result(parser_result, &parser);
        }
    }
}

static void rx_recover_discard_all(void)
{
    // Discard all buffered data immediately, including any valid frames.
    rx_finish_recovery();
}

static void rx_recover_drain_buffer(void)
{
    uint8_t c;

    // While overflow is set, the ISR does not store bytes; only this thread accesses the buffer.
    // Preserve parser state to complete frames using bytes received before the loss.
    // New bytes arriving during processing are dropped by the ISR; recovery is not lossless.
    while (ring_pop(&rx_ring, &c)) {
        ParserResult parser_result = parser_feed(&parser, c);
        process_parser_result(parser_result, &parser);
    }

    // Discard any incomplete frame remaining after the buffer has been drained.
    rx_finish_recovery();

}
static void rx_recover_overflow(void)
{
    // Call from the RX thread with interrupts enabled and the overflow flag set.
    switch (RX_OVERFLOW_POLICY) {
        case RX_OVERFLOW_DISCARD_ALL:
            rx_recover_discard_all();
            break;
        case RX_OVERFLOW_DRAIN_BUFFER:
            rx_recover_drain_buffer();
            break;
        default:
            // For an unknown policy, perform discard recovery before clearing the flag.
            rx_recover_discard_all();
            break;
    }
}

static void rx_finish_recovery(void)
{
    // Protect only the short recovery cleanup; do not log or run tracking here.
    unsigned int key = irq_lock();

    // Clear the software buffer and partial frame, then resume searching for STX.
    // This does not flush the hardware FIFO or guarantee alignment with the next frame.
    ring_init(&rx_ring);
    parser_init(&parser);

    // Allow the ISR to store bytes only after all recovery state has been reset.
    atomic_clear(&rx_overflow);

    irq_unlock(key);
}

static void process_parser_result(ParserResult result, const struct frame_parser *p_parser)
{
    switch(result) {
        case PARSER_FRAME_READY:
            submit_parsed_frame(p_parser);
            break;
        case PARSER_CRC_ERROR:
        case PARSER_LENGTH_ERROR:
        case PARSER_INCOMPLETE:
        default:
            // Do nothing
            break;
    }
}

static void submit_parsed_frame(const struct frame_parser *p_parser)
{
    struct received_frame received = {
        .type = p_parser->type,
        .payload_length = p_parser->payload_length,
        .received_at_ms = k_uptime_get(),
    };
    memcpy(received.payload, p_parser->payload, p_parser->payload_length);

    int ret = frame_handler_submit(&received);

    if (ret != 0) {
        // Count the new frame dropped because the queue is full.
        frame_queue_drop_count++;

        if (frame_queue_drop_count == 1 || frame_queue_drop_count % 100 == 0) {
            printk("Frame queue drops: %u\n", (unsigned int)frame_queue_drop_count);
        }
    }
}
