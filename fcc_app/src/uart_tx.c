#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/atomic.h>

#include <string.h>

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

    //TODO: tx init

    return 0;
}

bool uart_tx_submit(void)
{
    //TODO: tx submit
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
        }
    }
}