/*
 * learning-rebuild entry point
 *
 * Goal of this branch:
 * Rebuild the FCC/HIL pipeline from first principles instead of copying the
 * AI-generated implementation on main.
 *
 * Step 0: verify that the minimal Zephyr application boots and prints.
 * Step 1: implement UART polling RX/TX yourself.
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

int main(void)
{
    printk("=== FCC learning-rebuild boot ===\n");

    return 0;
}
