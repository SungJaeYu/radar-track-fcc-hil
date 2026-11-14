#ifndef TEST_LOGGING_LOG_H
#define TEST_LOGGING_LOG_H

#include <zephyr/sys/printk.h>

// Host tests capture message text through their existing printk implementation.
// Keep one capture call per message, including its terminating newline.
// This adapter does not emulate filtering, deferred processing, or buffer loss.
#define LOG_MODULE_REGISTER(...)
#define LOG_ERR(format, ...) printk(format "\n", ##__VA_ARGS__)
#define LOG_WRN(format, ...) printk(format "\n", ##__VA_ARGS__)
#define LOG_INF(format, ...) printk(format "\n", ##__VA_ARGS__)
#define LOG_DBG(format, ...) printk(format "\n", ##__VA_ARGS__)

#endif
