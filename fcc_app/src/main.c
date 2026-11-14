#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "transport/uart_rx.h"
#include "transport/uart_tx.h"

LOG_MODULE_REGISTER(radar_main, CONFIG_RADAR_MAIN_LOG_LEVEL);

int main(void)
{
    // Console diagnostics are separate from USART6 protocol frames.
    LOG_INF("=== FCC learning-rebuild boot ===");

    // Prepare the transmit path before enabling reception.
    // These init APIs return 0 on success and a negative errno on failure.
    int ret = uart_tx_init();
    if (ret != 0) {
        LOG_ERR("UART TX init failed %d", ret);
        return ret;
    }

    ret = uart_rx_init();
    if (ret != 0) {
        LOG_ERR("UART RX init failed %d", ret);
        return ret;
    }

    // Debug-only heartbeat; Zephyr processes each log message through its worker.
    while (1) {
        LOG_DBG("main alive");
        k_sleep(K_SECONDS(1));
    }

    return 0;
}
