#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/printk.h>

#include "ring_buffer.h"
#include "frame_parser.h"
#include "uart_rx.h"
#include "frame_handler.h"

// Set UART device
static const struct device *uart = DEVICE_DT_GET(DT_NODELABEL(usart6));

static struct ring_buffer rx_ring;
static struct frame_parser parser;
static struct frame_handler handler;

K_SEM_DEFINE(rx_sem, 0, 1);

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
                if (ring_push(&rx_ring, c)) {
                    k_sem_give(&rx_sem);
                }
            }
        }
    }
}

static void uart_rx_thread(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    while (1) {
        k_sem_take(&rx_sem, K_FOREVER);

        uint8_t c;
        while (ring_pop(&rx_ring, &c)) {
            if (parser_feed(&parser, c)) {
                // frame complete
                handle_frame(&handler, &parser);
            }
        }
    }
}
