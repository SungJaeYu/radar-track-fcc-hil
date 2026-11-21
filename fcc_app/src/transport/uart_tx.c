#include "transport/uart_tx.h"

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#if defined(CONFIG_RADAR_PERF_MONITOR)
#include "diagnostics/perf_monitor.h"
#endif

LOG_MODULE_REGISTER(uart_tx, CONFIG_RADAR_UART_TX_LOG_LEVEL);

static const struct device *p_uart = DEVICE_DT_GET(DT_NODELABEL(usart6));

static void uart_tx_thread(void *p_arg1, void *p_arg2, void *p_arg3);

// One-time startup barrier: queued requests must wait for successful UART init.
K_SEM_DEFINE(tx_ready_sem, 0, 1);

// The queue stores complete request copies; the TX thread alone emits their bytes.
K_MSGQ_DEFINE(tx_queue, sizeof(struct tx_frame), 8, 4);

K_THREAD_DEFINE(uart_tx_tid, 1024, uart_tx_thread, NULL, NULL, NULL, 7, 0, 0);

int uart_tx_init(void)
{
    const bool device_ready = device_is_ready(p_uart);
    if (!device_ready) {
        LOG_ERR("UART init: uart=USART6 status=not_ready");
        return -ENODEV;
    }

    k_sem_give(&tx_ready_sem);

    return 0;
}

int uart_tx_submit(const struct tx_frame *p_frame)
{
    if (p_frame == NULL) {
        return -EINVAL;
    }

    const bool length_valid = protocol_frame_has_valid_length(&p_frame->frame);
    if (!length_valid) {
        return -EINVAL;
    }

    const int ret = k_msgq_put(&tx_queue, p_frame, K_NO_WAIT);
#if defined(CONFIG_RADAR_PERF_MONITOR)
    perf_monitor_record_queue(PERF_QUEUE_TX, ret, k_msgq_num_used_get(&tx_queue));
#endif
    return ret;
}

static void uart_tx_thread(void *p_arg1, void *p_arg2, void *p_arg3)
{
    ARG_UNUSED(p_arg1);
    ARG_UNUSED(p_arg2);
    ARG_UNUSED(p_arg3);

    struct tx_frame request;

    // Wait until UART initialization succeeds.
    k_sem_take(&tx_ready_sem, K_FOREVER);

    while (1) {
        const int ret = k_msgq_get(&tx_queue, &request, K_FOREVER);

        if (ret == 0) {
            // Encode one complete queued request; discard only this request on encoding failure.
            uint8_t buffer[PROTOCOL_MAX_FRAME_SIZE];

            const int length = protocol_frame_encode(&request.frame, buffer, sizeof(buffer));

            if (length < 0) {
#if defined(CONFIG_RADAR_PERF_MONITOR)
                perf_monitor_record_tx(false);
#endif
                LOG_ERR("TX encode failed: error=%d", length);
                continue;
            }

            // Polling can wait for UART readiness and provides no peer acknowledgment.
            // Keep all frame bytes in this owner thread to preserve request order.
            for (int i = 0; i < length; ++i) {
                uart_poll_out(p_uart, buffer[i]);
            }
#if defined(CONFIG_RADAR_PERF_MONITOR)
            perf_monitor_record_tx(true);
#endif
        }
    }
}
