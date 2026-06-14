#pragma once
/*
 * uart_rx.h — HIL UART 수신 모듈 공개 인터페이스
 *
 * 정의 위치: uart_rx.c
 *   g_hil_uart  — USART6 장치 포인터 (tracking.c에서 참조)
 *   meas_msgq   — 측정값 큐 (tracking.c에서 k_msgq_get)
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/uart.h>
#include "frame.h"

/* USART6 장치 포인터 — uart_rx.c에서 정의 */
extern const struct device *g_hil_uart;

/* 측정값 큐 — uart_rx_thread가 넣고, tracking_thread가 꺼낸다 */
extern struct k_msgq meas_msgq;

/* 스레드 진입점 */
void uart_rx_thread(void *p1, void *p2, void *p3);
