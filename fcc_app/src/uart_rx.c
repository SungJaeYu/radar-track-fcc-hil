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
// 1이면 바이트 유실 상태: ISR은 새 입력을 버리고, 수신 스레드가 복구 후 해제한다.
static atomic_t rx_overflow = ATOMIC_INIT(0);

// 데이터 수신 또는 overflow 복구를 알린다. 실제 바이트는 rx_ring에 저장된다.
K_SEM_DEFINE(rx_sem, 0, 1);
// Keep the RX thread waiting until the ring, parser, and handler are initialized.
K_SEM_DEFINE(rx_ready_sem, 0, 1);

static void uart_callback(const struct device *dev, void *user_data);
static void uart_rx_thread(void *p1, void *p2, void *p3);

static void rx_recover_discard_all(void);
static void rx_recover_drain_buffer(void);
static void rx_recover_overflow(void);
static void rx_finish_recovery(void);

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
                // 복구 중에는 FIFO를 계속 읽되 새 바이트를 저장하지 않는다.
                // 기존 버퍼가 첫 유실 이전의 데이터로 유지되어야 drain할 수 있다.
                if (atomic_get(&rx_overflow) != 0) {
                    continue;
                }

                if (!ring_push(&rx_ring, c)) {
                    // 저장에 실패한 현재 바이트를 버리고 복구 필요 상태를 기록한다.
                    atomic_set(&rx_overflow, 1);
                }
                // 저장 성공뿐 아니라 최초 overflow 발생 때도 스레드를 깨운다.
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

        while (1) {
            // Check even when the receive buffer is empty.
            frame_handler_check_timeout(&handler, k_uptime_get());

            uint8_t c;

            // overflow 확인부터 한 바이트 파싱까지 UART ISR이 끼어들지 않게 한다.
            unsigned int key = irq_lock();

            if (atomic_get(&rx_overflow) != 0) {
                // drain의 추적 계산·로그 출력 동안에는 인터럽트를 허용한다.
                // 플래그는 아직 1이므로 ISR의 링버퍼 쓰기는 계속 중단된다.
                irq_unlock(key);

                rx_recover_overflow();

                printk("RX overflow: recovery completed\n");

                break;
            }

            if (!ring_pop(&rx_ring, &c)) {
                irq_unlock(key);
                break;
            }

            bool frame_complete = parser_feed(&parser, c);

            irq_unlock(key);

            if (frame_complete) {
                // 완성된 프레임의 추적 계산과 로그는 잠금 밖에서 처리한다.
                handle_frame(&handler, &parser);
            }
        }
    }
}

static void rx_recover_discard_all(void)
{
    // 버퍼에 남은 정상 데이터도 처리하지 않고 즉시 폐기한다.
    rx_finish_recovery();
}

static void rx_recover_drain_buffer(void)
{
    uint8_t c;

    // overflow=1 동안 ISR은 저장하지 않으므로 이 스레드만 버퍼를 접근한다.
    // 기존 파서 상태를 유지하여 유실 이전 바이트로 완성되는 프레임을 처리한다.
    // 처리 중 도착하는 새 바이트는 ISR에서 버리므로 무손실 복구는 아니다.
    while (ring_pop(&rx_ring, &c)) {
        frame_handler_check_timeout(&handler, k_uptime_get());

        if (parser_feed(&parser, c)) {
            handle_frame(&handler, &parser);
        }
    }

    // 버퍼를 다 읽은 뒤에도 완성되지 않은 프레임은 폐기한다.
    rx_finish_recovery();

}
static void rx_recover_overflow(void)
{
    // 수신 스레드에서 인터럽트가 허용되고 overflow=1인 상태로 호출한다.
    switch (RX_OVERFLOW_POLICY) {
        case RX_OVERFLOW_DISCARD_ALL:
            rx_recover_discard_all();
            break;
        case RX_OVERFLOW_DRAIN_BUFFER:
            rx_recover_drain_buffer();
            break;
        default:
            // 알 수 없는 설정에서도 복구 없이 플래그만 해제하지 않는다.
            rx_recover_discard_all();
            break;
    }
}

static void rx_finish_recovery(void)
{
    // 짧은 공통 마무리만 보호한다. 이 구간에서는 로그·추적 계산을 하지 않는다.
    unsigned int key = irq_lock();

    // 소프트웨어 버퍼와 조립 중인 프레임을 비우고 STX 탐색부터 다시 시작한다.
    // 하드웨어 FIFO를 비우거나 다음 프레임 경계를 보장하는 처리는 아니다.
    ring_init(&rx_ring);
    parser_init(&parser);

    // 초기화가 모두 끝난 뒤에만 ISR의 저장을 다시 허용한다.
    atomic_clear(&rx_overflow);

    irq_unlock(key);
}
