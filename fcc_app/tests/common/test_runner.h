#ifndef TEST_RUNNER_H
#define TEST_RUNNER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

struct test_case {
    const char *p_name;
    void (*p_run)(void);
};

#define TEST_CASE(name) {#name, name}

// With no name, run all cases. A name selects one case for CTest or debugging.
static int run_tests(int argc, char **p_argv, const struct test_case *p_cases, size_t count)
{
    bool matched = false;
    for (size_t i = 0; i < count; ++i) {
        if (argc == 1 || (argc == 2 && strcmp(p_argv[1], p_cases[i].p_name) == 0)) {
            p_cases[i].p_run();
            printf("PASS: %s\n", p_cases[i].p_name);
            matched = true;
        }
    }
    if (!matched) {
        fprintf(stderr, "Usage: %s [test_name]\n", p_argv[0]);
        return 2;
    }
    return 0;
}

#endif
