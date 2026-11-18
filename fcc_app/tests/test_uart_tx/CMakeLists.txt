cmake_minimum_required(VERSION 3.20.0)
project(test_uart_tx C)
enable_testing()
add_executable(uart_tx_tests src/main.c ../../src/protocol/protocol_frame.c
    ../../src/common/crc16.c ../../src/protocol/frame_parser.c)
target_include_directories(uart_tx_tests PRIVATE include ../common/include ../../src)
target_compile_features(uart_tx_tests PRIVATE c_std_11)
target_compile_options(uart_tx_tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)
foreach(test_name IN ITEMS test_tx_init test_tx_invalid_submit
        test_tx_queue_copy_fifo_full_and_recovery test_tx_encode_error_continues)
    add_test(NAME ${test_name} COMMAND uart_tx_tests ${test_name})
endforeach()

# Re-run the same behavior tests with the real performance collector enabled.
option(PERF_TEST_ENABLED "Exercise optional performance hooks" OFF)
if(PERF_TEST_ENABLED)
    find_package(Threads REQUIRED)
    target_sources(uart_tx_tests PRIVATE ../../src/diagnostics/perf_monitor.c)
    target_include_directories(uart_tx_tests PRIVATE ../test_perf_monitor/include)
    target_compile_definitions(uart_tx_tests PRIVATE CONFIG_RADAR_PERF_MONITOR=1)
    target_link_libraries(uart_tx_tests PRIVATE Threads::Threads)
endif()
