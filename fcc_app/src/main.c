#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#if defined(CONFIG_RADAR_PERF_MONITOR)
#include "diagnostics/perf_monitor.h"
#endif
#include "transport/uart_rx.h"
#include "transport/uart_tx.h"
#include "tracking/estimator.h"

LOG_MODULE_REGISTER(radar_main, CONFIG_RADAR_MAIN_LOG_LEVEL);

int main(void)
{
    // Console diagnostics are separate from USART6 protocol frames.
    LOG_INF("=== FCC learning-rebuild boot ===");
    LOG_INF("Tracking estimator: %s", estimator_name());

    // Prepare the transmit path before enabling reception.
    // These init APIs return 0 on success and a negative errno on failure.
    int ret = uart_tx_init();
    if (ret != 0) {
        return ret;
    }

    ret = uart_rx_init();
    if (ret != 0) {
        return ret;
    }

    LOG_INF("FCC ready: USART6 RX/TX initialized");

    // Reuse main for low-rate diagnostics; normal builds keep the idle loop.
    while (1) {
#if defined(CONFIG_RADAR_PERF_MONITOR)
        k_sleep(K_SECONDS(5));
        perf_monitor_log_summary();
#else
        k_sleep(K_SECONDS(1));
#endif
    }

    return 0;
}
