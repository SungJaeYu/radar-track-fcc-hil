#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(transport_stress, LOG_LEVEL_INF);

extern const k_tid_t uart_rx_tid;
extern const k_tid_t frame_handler_tid;
extern const k_tid_t uart_tx_tid;

static void pause_consumer(k_tid_t thread, const char *p_name)
{
    // Suspend only thread execution; UART interrupts keep receiving bytes.
    k_thread_suspend(thread);
    LOG_INF("BENCH %s: state=paused", p_name);
    k_sleep(K_SECONDS(4));
    k_thread_resume(thread);
    LOG_INF("BENCH %s: state=resumed", p_name);
    k_sleep(K_SECONDS(4));
}

static void transport_stress_thread(void *p_arg1, void *p_arg2, void *p_arg3)
{
    ARG_UNUSED(p_arg1);
    ARG_UNUSED(p_arg2);
    ARG_UNUSED(p_arg3);
    k_sleep(K_SECONDS(8));
    pause_consumer(uart_rx_tid, "RX");
    pause_consumer(frame_handler_tid, "HANDLER");
    pause_consumer(uart_tx_tid, "TX");
    LOG_INF("BENCH: state=complete");
}

// Higher priority than consumers; sleeping still permits ISR and logger progress.
K_THREAD_DEFINE(transport_stress_tid, 1024, transport_stress_thread,
                NULL, NULL, NULL, 0, 0, 0);
