cmake_minimum_required(VERSION 3.20.0)
project(test_uart_rx C)
enable_testing()

add_executable(uart_rx_tests
    src/main.c
    ../../src/common/ring_buffer.c
    ../../src/protocol/protocol_frame.c
    ../../src/common/crc16.c
    ../../src/protocol/frame_parser.c
)
target_include_directories(uart_rx_tests PRIVATE include ../../src)
target_compile_features(uart_rx_tests PRIVATE c_std_11)
# Assertions drive this harness and must remain enabled in Release builds too.
target_compile_options(uart_rx_tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)

foreach(test_name IN ITEMS
    test_stats_snapshot_and_reset
    test_timeout_boundary_for_every_partial_frame
    test_timeout_ignores_idle_and_complete_frames
    test_timeout_recovers_next_valid_frame
    test_timeout_preserves_split_frames
    test_timeout_refreshes_on_each_byte
    test_explicit_reset_cancels_partial_timeout

    test_processor_copies_frame_and_reset_preserves_drops
    test_nonready_results_do_not_submit
    test_queue_full_counts_drop_and_recovers
    test_drain_queue_full_still_finishes_recovery

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
