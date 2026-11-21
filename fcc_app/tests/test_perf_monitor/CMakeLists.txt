cmake_minimum_required(VERSION 3.20.0)
project(test_perf_monitor C)
enable_testing()
find_package(Threads REQUIRED)
add_executable(perf_monitor_tests src/main.c)
target_include_directories(perf_monitor_tests PRIVATE include ../common/include ../../src)
target_compile_features(perf_monitor_tests PRIVATE c_std_11)
target_compile_options(perf_monitor_tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)
target_link_libraries(perf_monitor_tests PRIVATE Threads::Threads)
foreach(test_name IN ITEMS test_execution_aggregation test_counter_saturation test_timing_boundaries test_failed_and_missing_submit
        test_context_cleanup test_invalid_timestamps test_queue_and_tx_counts
        test_snapshot_contract test_concurrent_snapshots)
    add_test(NAME ${test_name} COMMAND perf_monitor_tests ${test_name})
endforeach()

add_executable(perf_reporter_tests src/reporter.c
    ../../src/diagnostics/perf_monitor.c ../../src/diagnostics/perf_reporter.c)
target_include_directories(perf_reporter_tests PRIVATE include ../common/include ../../src)
target_compile_features(perf_reporter_tests PRIVATE c_std_11)
target_compile_options(perf_reporter_tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)
target_link_libraries(perf_reporter_tests PRIVATE Threads::Threads)
add_test(NAME test_summary_and_stack_queries COMMAND perf_reporter_tests test_summary_and_stack_queries)

add_test(NAME test_execution_summary_cycles COMMAND perf_reporter_tests test_execution_summary)

add_executable(perf_reporter_timing_tests src/reporter.c
    ../../src/diagnostics/perf_monitor.c ../../src/diagnostics/perf_reporter.c)
target_include_directories(perf_reporter_timing_tests PRIVATE include ../common/include ../../src)
target_compile_features(perf_reporter_timing_tests PRIVATE c_std_11)
target_compile_options(perf_reporter_timing_tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)
target_compile_definitions(perf_reporter_timing_tests PRIVATE CONFIG_TIMING_FUNCTIONS=1)
target_link_libraries(perf_reporter_timing_tests PRIVATE Threads::Threads)
add_test(NAME test_execution_summary_ns COMMAND perf_reporter_timing_tests test_execution_summary)
