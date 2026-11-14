# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)
option(TRACK_TEST_HOST "Run the pure C tests without Zephyr" OFF)

if(TRACK_TEST_HOST)
    project(test_kalman C)
    enable_testing()
    add_executable(track_tests src/main.c)
    target_compile_definitions(track_tests PRIVATE TRACK_TEST_HOST)
    target_include_directories(track_tests PRIVATE ../common/include)
    target_compile_options(track_tests PRIVATE -Wall -Wextra -Werror)
    target_link_libraries(track_tests PRIVATE m)
    set(TEST_TARGET track_tests)
    add_test(NAME tracking COMMAND track_tests)
else()
    find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
    project(test_kalman)
    target_sources(app PRIVATE src/main.c)
    set(TEST_TARGET app)
endif()

target_sources(${TEST_TARGET} PRIVATE
    ../../src/tracking/kalman_cv.c
    ../../src/tracking/angle_utils.c
    ../../src/tracking/gating.c
    ../../src/tracking/track.c
    ../../src/protocol/radar_message.c
)
target_include_directories(${TEST_TARGET} PRIVATE ../../src)
