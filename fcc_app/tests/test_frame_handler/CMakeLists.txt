cmake_minimum_required(VERSION 3.20.0)
project(test_frame_handler C)
enable_testing()

add_executable(frame_handler_tests src/main.c
    ../../src/track.c ../../src/kalman_cv.c ../../src/angle_utils.c
    ../../src/gating.c ../../src/radar_message.c)
target_include_directories(frame_handler_tests PRIVATE include ../../src)
target_compile_features(frame_handler_tests PRIVATE c_std_11)
target_compile_options(frame_handler_tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)
target_link_libraries(frame_handler_tests PRIVATE m)

foreach(test_name IN ITEMS test_status_zero test_status_snapshot
        test_status_preserves_track test_status_does_not_arm_timeout
        test_status_does_not_refresh_timeout)
    add_test(NAME ${test_name} COMMAND frame_handler_tests ${test_name})
endforeach()
