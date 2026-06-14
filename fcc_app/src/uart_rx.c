/*
 * uart_rx.c — HIL UART 수신 모듈
 * USART6 ISR → 링버퍼 → FrameParser → meas_msgq (FccMeasPayload)
 */

#include "uart_rx.h"
#include <zephyr/sys/printk.h>
#include <zephyr/sys/ring_buffer.h>

/* USART6 장치 포인터 */
const struct device *g_hil_uart = DEVICE_DT_GET(DT_NODELABEL(usart6));

/* ISR ↔ uart_rx_thread 간 바이트 버퍼 */
#define HIL_RX_BUF_SIZE 256U
RING_BUF_DECLARE(g_uart_rx_ring, HIL_RX_BUF_SIZE);
static K_SEM_DEFINE(g_uart_rx_sem, 0, 1);

/* ISR 컨텍스트 — printk 불가, 오버플로우 횟수만 카운트 */
static volatile uint32_t g_rx_overflow_cnt;

/* 측정값 큐 */
K_MSGQ_DEFINE(meas_msgq, sizeof(FccMeasPayload), 8, 4);

/* ISR: FIFO 바이트 → 링버퍼, 스레드 깨움 */
static void hil_uart_isr(const struct device *dev, void *user_data)
{
    ARG_UNUSED(user_data);
    uint8_t byte;
    bool    got_data = false;

    uart_irq_update(dev);
    while (uart_irq_is_pending(dev)) {
        if (!uart_irq_rx_ready(dev)) {
            break;
        }
        while (uart_fifo_read(dev, &byte, 1) > 0) {
            if (ring_buf_put(&g_uart_rx_ring, &byte, 1) == 0) {
                g_rx_overflow_cnt++;
            }
            got_data = true;
        }
    }

    if (got_data) {
        k_sem_give(&g_uart_rx_sem);
    }
}

/* UART RX 스레드: 프레임 수신·파싱 → meas_msgq에 put */
void uart_rx_thread(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

    FrameParser parser;
    FccFrame    frame;

    frame_parser_init(&parser);

    if (!device_is_ready(g_hil_uart)) {
        printk("[uart_rx ] USART6 준비 안됨 - HIL 비활성\n");
        while (1) {
            k_sleep(K_SECONDS(10));
        }
    }

    uart_irq_callback_set(g_hil_uart, hil_uart_isr);
    uart_irq_rx_enable(g_hil_uart);

    printk("[uart_rx ] USART6 준비 완료, HIL 수신 시작\n");

    while (1) {
        k_sem_take(&g_uart_rx_sem, K_FOREVER);

        uint8_t byte;

        while (ring_buf_get(&g_uart_rx_ring, &byte, 1) == 1) {
            if (!frame_parser_feed(&parser, byte, &frame)) {
                continue;
            }

            if (frame.msg_type == FCC_MSG_MEAS &&
                frame.payload_len == sizeof(FccMeasPayload)) {

                const FccMeasPayload *mp = (const FccMeasPayload *)frame.payload;
                FccMeasPayload msg = *mp;

                if (k_msgq_put(&meas_msgq, &msg, K_NO_WAIT) != 0) {
                    k_msgq_purge(&meas_msgq);
                    printk("[uart_rx ] meas_msgq 포화, 퍼지 (링버퍼 오버플로: %u회)\n",
                           g_rx_overflow_cnt);
                    g_rx_overflow_cnt = 0;
                }

            } else if (frame.msg_type == FCC_MSG_CTRL &&
                       frame.payload_len == sizeof(FccCtrlPayload)) {

                const FccCtrlPayload *cp = (const FccCtrlPayload *)frame.payload;

                if (cp->cmd == FCC_CTRL_RESET) {
                    frame_parser_reset(&parser);
                    k_msgq_purge(&meas_msgq);
                    printk("[uart_rx ] CTRL_RESET\n");
                }
            }
        }
    }
}
