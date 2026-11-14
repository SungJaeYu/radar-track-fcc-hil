#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(log_stress, LOG_LEVEL_DBG);

static void log_stress_thread(void *p_arg1, void *p_arg2, void *p_arg3)
{
    ARG_UNUSED(p_arg1);
    ARG_UNUSED(p_arg2);
    ARG_UNUSED(p_arg3);

    // Allow the PC to establish baseline traffic after flashing.
    k_sleep(K_SECONDS(8));
    LOG_INF("Log stress starting: bursts=10, messages_per_burst=2000");
    k_sleep(K_MSEC(200));

    for (unsigned int burst = 0; burst < 10; ++burst) {
        for (unsigned int index = 0; index < 2000; ++index) {
            LOG_DBG("Log stress sequence=%u payload=0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ",
                    burst * 2000 + index);
        }
        // Let the logger run between bursts; RX/handler/TX have higher priorities.
        k_sleep(K_MSEC(100));
    }

    // Allow the buffer to drain so the completion marker can be queued.
    k_sleep(K_SECONDS(2));
    LOG_INF("Log stress complete: attempted=20000");
    // Returning terminates this one-shot test thread.
}

K_THREAD_DEFINE(log_stress_tid, 1024, log_stress_thread, NULL, NULL, NULL, 10, 0, 0);
