#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

// for UART
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>

#include "uart_rx.h"

int main(void)
{
    // Boot Signal
    printk("=== FCC learning-rebuild boot ===\n");
    
    if (uart_rx_init() != 0) {
        printk("UART RX init failed\n");
        return -1;
    }

    while (1) {
        printk("main alive\n");
        k_sleep(K_SECONDS(1));
    }

    return 0;
}
