/*
 * main.c — FCC HIL 진입점
 * 스레드 스택·TCB 정의 + 3개 스레드 생성만 담당.
 * UART / 트래킹 / 디스플레이 로직은 각 모듈로 분리됨.
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include "uart_rx.h"
#include "tracking.h"
#include "display.h"

#define STACK_UART_RX  2048
#define STACK_TRACKING 4096
#define STACK_DISPLAY  2048

K_THREAD_STACK_DEFINE(uart_rx_stack,  STACK_UART_RX);
K_THREAD_STACK_DEFINE(tracking_stack, STACK_TRACKING);
K_THREAD_STACK_DEFINE(display_stack,  STACK_DISPLAY);

static struct k_thread uart_rx_td;
static struct k_thread tracking_td;
static struct k_thread display_td;

int main(void)
{
    printk("=== FCC HIL booting ===\n");

    k_thread_create(&uart_rx_td, uart_rx_stack,
                    K_THREAD_STACK_SIZEOF(uart_rx_stack),
                    uart_rx_thread, NULL, NULL, NULL,
                    5, 0, K_NO_WAIT);
    k_thread_name_set(&uart_rx_td, "uart_rx");

    k_thread_create(&tracking_td, tracking_stack,
                    K_THREAD_STACK_SIZEOF(tracking_stack),
                    tracking_thread, NULL, NULL, NULL,
                    6, 0, K_NO_WAIT);
    k_thread_name_set(&tracking_td, "tracking");

    k_thread_create(&display_td, display_stack,
                    K_THREAD_STACK_SIZEOF(display_stack),
                    display_thread, NULL, NULL, NULL,
                    7, 0, K_NO_WAIT);
    k_thread_name_set(&display_td, "display");

    return 0;
}
