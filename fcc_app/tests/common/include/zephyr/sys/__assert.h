#ifndef TEST_ZEPHYR_SYS_ASSERT_H
#define TEST_ZEPHYR_SYS_ASSERT_H

#include <stdio.h>
#include <stdlib.h>

// Host-only assertion adapter. Keep checks active independently of NDEBUG.
// Do not use this include directory in firmware builds.
#define __ASSERT(condition, ...)                                                                   \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            fprintf(stderr, "Assertion failed: %s\n", #condition);                                 \
            fprintf(stderr, __VA_ARGS__);                                                          \
            fputc('\n', stderr);                                                                   \
            abort();                                                                               \
        }                                                                                          \
    } while (0)

#endif
