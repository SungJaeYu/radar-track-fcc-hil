cmake_minimum_required(VERSION 3.20.0)
project(test_uart_rx C)
enable_testing()

add_executable(uart_rx_tests
    src/main.c
    ../../src/ring_buffer.c
    ../../src/frame_parser.c
)
target_include_directories(uart_rx_tests PRIVATE include ../../src)
target_compile_features(uart_rx_tests PRIVATE c_std_11)
# Assertions drive this harness and must remain enabled in Release builds too.
target_compile_options(uart_rx_tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)

foreach(test_name IN ITEMS
    test_dispatch_drains_complete_frame
    test_preserves_partial_parser_and_drops_new_input
    test_discards_incomplete_frame
    test_rejects_bad_crc
    test_drain_rejects_bad_length_then_handles_valid_frame
    test_receives_after_recovery
    test_empty_recovery
    test_ring_overflow_preserves_buffer
    test_ring_wraparound
)
    add_test(NAME ${test_name} COMMAND uart_rx_tests ${test_name})
endforeach()
