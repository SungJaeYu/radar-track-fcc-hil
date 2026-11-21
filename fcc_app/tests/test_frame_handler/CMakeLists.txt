cmake_minimum_required(VERSION 3.20.0)
project(test_frame_handler C)
enable_testing()

# Host tests do not run Kconfig; allow independent timeout boundary testing.
set(RADAR_MEASUREMENT_TIMEOUT_MS 1000 CACHE STRING "Timeout under test in milliseconds")

add_executable(frame_handler_tests src/main.c
    ../../src/application/radar_processor.c
    ../../src/tracking/estimator.c
    ../../src/tracking/track.c ../../src/tracking/kalman_cv.c ../../src/tracking/angle_utils.c
    ../../src/tracking/gating.c ../../src/protocol/measurement_message.c
    ../../src/protocol/protocol_frame.c ../../src/common/crc16.c
    ../../src/protocol/status_message.c ../../src/protocol/track_message.c
    ../../src/protocol/frame_parser.c)
target_include_directories(frame_handler_tests PRIVATE include ../common/include ../../src)
target_compile_definitions(frame_handler_tests PRIVATE
    CONFIG_RADAR_MEASUREMENT_TIMEOUT_MS=${RADAR_MEASUREMENT_TIMEOUT_MS}
    CONFIG_RADAR_RANGE_GATE_M=100 CONFIG_RADAR_AZIMUTH_GATE_DEG=10)
target_compile_features(frame_handler_tests PRIVATE c_std_11)
target_compile_options(frame_handler_tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)
target_link_libraries(frame_handler_tests PRIVATE m)

foreach(test_name IN ITEMS
        test_stale_measurement_boundary_and_stats
        test_processor_invalid_input_preserves_output
        test_processor_timeout_query_preserves_state
        test_track_encode_wire
        test_track_encode_invalid_arguments
        test_track_started_updated_and_confirmed
        test_track_timestamp_rejection_report
        test_track_gate_rejection_empty_and_lost
        test_track_spike_recovery_matrix
        test_track_gap_reset_and_recovery
        test_track_sequence_wrap_and_timeout_preservation
        test_track_submit_failure_and_recovery
        test_track_encode_failure_and_recovery
        test_incoming_track_report_is_ignored
        test_init_command_resets_tracking
        test_reset_command_resets_tracking
        test_valid_measurements_arm_refresh_and_expire
        test_invalid_measurements_do_not_arm_timeout
        test_invalid_measurements_do_not_refresh_timeout
        test_gate_rejection_refreshes_receive_timeout
        test_timestamp_rejection_refreshes_receive_timeout
        test_status_zero test_status_snapshot
        test_status_preserves_track test_status_does_not_arm_timeout
        test_status_does_not_refresh_timeout
        test_status_encode_wire
        test_status_encode_invalid_arguments
        test_status_submit_failure_and_recovery
        test_status_encode_failure_does_not_submit
        test_incoming_status_report_is_ignored)
    add_test(NAME ${test_name} COMMAND frame_handler_tests ${test_name})
endforeach()

# Re-run the same behavior tests with the real performance collector enabled.
option(PERF_TEST_ENABLED "Exercise optional performance hooks" OFF)
if(PERF_TEST_ENABLED)
    find_package(Threads REQUIRED)
    target_sources(frame_handler_tests PRIVATE ../../src/diagnostics/perf_monitor.c)
    target_include_directories(frame_handler_tests PRIVATE ../test_perf_monitor/include)
    target_compile_definitions(frame_handler_tests PRIVATE CONFIG_RADAR_PERF_MONITOR=1)
    target_link_libraries(frame_handler_tests PRIVATE Threads::Threads)
    add_test(NAME test_perf_measurement_paths COMMAND frame_handler_tests test_perf_measurement_paths)
endif()
