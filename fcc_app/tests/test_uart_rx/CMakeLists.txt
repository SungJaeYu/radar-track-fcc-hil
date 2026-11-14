cmake_minimum_required(VERSION 3.20.0)
project(test_uart_rx C)
enable_testing()

# Host tests do not run Kconfig; allow independent timeout boundary testing.
set(RADAR_FRAME_RX_TIMEOUT_MS 1000 CACHE STRING "Timeout under test in milliseconds")

# Host builds do not run Zephyr Kconfig. Define exactly one policy per target.
function(add_rx_target target policy)
    add_executable(${target}
        src/main.c
        ../../src/common/ring_buffer.c
        ../../src/protocol/protocol_frame.c
        ../../src/common/crc16.c
        ../../src/protocol/frame_parser.c
    )
    target_include_directories(${target} PRIVATE include ../common/include ../../src)
    target_compile_features(${target} PRIVATE c_std_11)
    target_compile_definitions(${target} PRIVATE ${policy}=1
        CONFIG_RADAR_FRAME_RX_TIMEOUT_MS=${RADAR_FRAME_RX_TIMEOUT_MS})
    # Keep assertions active in Release builds too.
    target_compile_options(${target} PRIVATE -Wall -Wextra -Werror -UNDEBUG)
endfunction()

# Keep the existing drain executable name for scripts and editor tasks.
add_rx_target(uart_rx_tests CONFIG_RADAR_RX_OVERFLOW_DRAIN)
add_rx_target(uart_rx_discard_tests CONFIG_RADAR_RX_OVERFLOW_DISCARD)

set(common_cases
    test_rx_init_errors
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
    test_discards_incomplete_frame
    test_receives_after_recovery
    test_empty_recovery
    test_ring_overflow_preserves_buffer
    test_ring_wraparound
)
set(drain_cases
    test_dispatch_drains_complete_frame
    test_preserves_partial_parser_and_drops_new_input
    test_rejects_bad_crc
    test_drain_rejects_bad_length_then_handles_valid_frame
    test_drain_queue_full_still_finishes_recovery
)
foreach(test_name IN LISTS common_cases drain_cases)
    add_test(NAME ${test_name} COMMAND uart_rx_tests ${test_name})
endforeach()
foreach(test_name IN LISTS common_cases)
    add_test(NAME discard_${test_name} COMMAND uart_rx_discard_tests ${test_name})
endforeach()
foreach(test_name IN ITEMS test_discard_drops_complete_frame_and_recovers
                           test_discard_drops_completable_partial_frame)
    add_test(NAME ${test_name} COMMAND uart_rx_discard_tests ${test_name})
endforeach()
