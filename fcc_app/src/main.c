#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "transport/uart_rx.h"
#include "transport/uart_tx.h"

int main(void)
{
    // Console diagnostics are separate from USART6 protocol frames.
    printk("=== FCC learning-rebuild boot ===\n");

    // Prepare the transmit path before enabling reception.
    // These init APIs return 0 on success and a negative errno on failure.
    int ret = uart_tx_init();
    if (ret != 0) {
        printk("UART TX init failed %d\n", ret);
        return ret;
    }

    ret = uart_rx_init();
    if (ret != 0) {
        printk("UART RX init failed %d\n", ret);
        return ret;
    }

    // This heartbeat may interleave with other printk calls; deferred logging is planned.
    while (1) {
        printk("main alive\n");
        k_sleep(K_SECONDS(1));
    }

    return 0;
}
