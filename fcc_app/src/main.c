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
        return ret;
    }

    ret = uart_rx_init();
    if (ret != 0) {
        return ret;
    }

    LOG_INF("FCC ready: USART6 RX/TX initialized");

    // Keep the existing idle loop; periodic heartbeat output adds no tracking evidence.
    while (1) {
        k_sleep(K_SECONDS(1));
    }

    return 0;
}
