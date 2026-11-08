#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/atomic.h>

#include <string.h>
#include <errno.h>

#include "uart_tx.h"

static const struct device *p_uart = DEVICE_DT_GET(DT_NODELABEL(usart6));

static void uart_tx_thread(void *p_arg1, void *p_arg2, void *p_arg3);

K_SEM_DEFINE(tx_ready_sem, 0, 1);

K_MSGQ_DEFINE(tx_queue, sizeof(struct send_frame), 8, 4);

K_THREAD_DEFINE(uart_tx_tid, 1024, uart_tx_thread, NULL, NULL, NULL, 7, 0, 0);

int uart_tx_init(void)
{
    // Check UART device is ready
    if (!device_is_ready(p_uart)) {
        // Error Handling
        printk("USART6 is not ready\n");
        return -1;
    }

    k_sem_give(&tx_ready_sem);

    return 0;
}

int uart_tx_submit(const struct send_frame *p_s_frame)
{
    if (p_s_frame == NULL || !protocol_frame_is_valid(&p_s_frame->frame)) {
        return -EINVAL;
    }

    return k_msgq_put(&tx_queue, p_s_frame, K_NO_WAIT);
}

static void uart_tx_thread(void *p_arg1, void *p_arg2, void *p_arg3)
{
    ARG_UNUSED(p_arg1);
    ARG_UNUSED(p_arg2);
    ARG_UNUSED(p_arg3);

    struct send_frame request;

    // Wait until UART initialization succeeds.
    k_sem_take(&tx_ready_sem, K_FOREVER);

    while (1) {
        int ret = k_msgq_get(&tx_queue, &request, K_FOREVER);

        if (ret == 0) {
            // Encode the frame and send it
            uint8_t buffer[MAX_PAYLOAD + 5U];

            int length = protocol_frame_encode(&request.frame, buffer, sizeof(buffer));

            if (length < 0) {
                printk("TX encode failed: %d\n", length);
                continue;
            }

            for (int i = 0; i < length; ++i) {
                uart_poll_out(p_uart, buffer[i]);
            }
        }
    }
}