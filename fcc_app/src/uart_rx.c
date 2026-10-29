#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/atomic.h>

#include "ring_buffer.h"
#include "frame_parser.h"
#include "uart_rx.h"
#include "frame_handler.h"

// Set UART device
static const struct device *uart = DEVICE_DT_GET(DT_NODELABEL(usart6));

static struct ring_buffer rx_ring;
static struct frame_parser parser;
static struct frame_handler handler;
static atomic_t rx_overflow = ATOMIC_INIT(0);

// Notify the RX thread of buffered data; the bytes remain in rx_ring.
K_SEM_DEFINE(rx_sem, 0, 1);
// Keep the RX thread waiting until the ring, parser, and handler are initialized.
K_SEM_DEFINE(rx_ready_sem, 0, 1);

static void uart_callback(const struct device *dev, void *user_data);
static void uart_rx_thread(void *p1, void *p2, void *p3);

K_THREAD_DEFINE(uart_rx_tid, 1024, uart_rx_thread, NULL, NULL, NULL, 5, 0, 0);

int uart_rx_init(void) {
    // Check UART device is ready
    if (!device_is_ready(uart)) {
        // Error Handling
        printk("USART6 is not ready\n");
        return -1;
    }

    // Set Callback Function
    if(uart_irq_callback_user_data_set(uart, uart_callback, NULL) != 0) {
        // error
        printk("Failed to set UART callback\n");
        return -1;
    }

    ring_init(&rx_ring);

    // init Parser
    parser_init(&parser);

    // init Handler
    frame_handler_init(&handler);

    // Enable RX Interrupt
    uart_irq_rx_enable(uart);

    // Signal that RX initialization is completed
    k_sem_give(&rx_ready_sem);

    return 0;
}

static void uart_callback(const struct device *dev, void *user_data) {
    // Avoid Warning
    ARG_UNUSED(user_data);

    // UART Iterrupt Update
    uart_irq_update(dev);

    // UART Interrupt is pending
    while (uart_irq_is_pending(dev)) {
        // if UART Interrupt Receive Ready
        if (uart_irq_rx_ready(dev)) {
            uint8_t c;
            // UART FIFO Read
            while(uart_fifo_read(dev, &c, 1) == 1) {
                // drop new byte after overflow before recover
                if (atomic_get(&rx_overflow) != 0) {
                    continue;
                }

                if (!ring_push(&rx_ring, c)) {
                    atomic_set(&rx_overflow, 1);
                }
                k_sem_give(&rx_sem);
            }
        }
    }
}

static void uart_rx_thread(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    // Wait for RX initialization to complete
    k_sem_take(&rx_ready_sem, K_FOREVER);

    while (1) {
        // Wake on RX notification, or after 50 ms even when no data arrives.
        // This wait interval is separate from the measurement timeout threshold.
        k_sem_take(&rx_sem, K_MSEC(50));

        // Check even when the receive buffer is empty.
        frame_handler_check_timeout(&handler, k_uptime_get());

        uint8_t c;
        while (ring_pop(&rx_ring, &c)) {

            // Keep checking while continuous input keeps the buffer nonempty.
            // Expire the old track before processing a new measurement.
            frame_handler_check_timeout(&handler, k_uptime_get());

            if (parser_feed(&parser, c)) {
                // frame complete
                handle_frame(&handler, &parser);
            }
        }
    }
}
