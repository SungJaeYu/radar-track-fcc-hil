cmake_minimum_required(VERSION 3.20.0)
project(test_frame_handler C)
enable_testing()

add_executable(frame_handler_tests src/main.c
    ../../src/tracking/track.c ../../src/tracking/kalman_cv.c ../../src/tracking/angle_utils.c
    ../../src/tracking/gating.c ../../src/protocol/radar_message.c
    ../../src/protocol/protocol_frame.c ../../src/common/crc16.c
    ../../src/protocol/status_message.c ../../src/protocol/frame_parser.c)
target_include_directories(frame_handler_tests PRIVATE include ../../src)
target_compile_features(frame_handler_tests PRIVATE c_std_11)
target_compile_options(frame_handler_tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)
target_link_libraries(frame_handler_tests PRIVATE m)

foreach(test_name IN ITEMS test_status_zero test_status_snapshot
        test_status_preserves_track test_status_does_not_arm_timeout
        test_status_does_not_refresh_timeout
        test_status_encode_wire
        test_status_encode_invalid_arguments
        test_status_submit_failure_and_recovery
        test_status_encode_failure_does_not_submit
        test_incoming_status_report_is_ignored)
    add_test(NAME ${test_name} COMMAND frame_handler_tests ${test_name})
endforeach()
