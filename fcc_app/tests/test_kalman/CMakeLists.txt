# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)
option(TRACK_TEST_HOST "Run the pure C tests without Zephyr" OFF)

if(TRACK_TEST_HOST)
    project(test_kalman C)
    enable_testing()
    add_executable(track_tests src/main.c)
    target_compile_definitions(track_tests PRIVATE TRACK_TEST_HOST)
    # Existing tracking scenarios exercise the unchanged production defaults.
    target_compile_definitions(track_tests PRIVATE
        CONFIG_RADAR_RANGE_GATE_M=100 CONFIG_RADAR_AZIMUTH_GATE_DEG=10)
    target_include_directories(track_tests PRIVATE ../common/include)
    target_compile_options(track_tests PRIVATE -Wall -Wextra -Werror)
    target_link_libraries(track_tests PRIVATE m)
    set(TEST_TARGET track_tests)
    add_test(NAME tracking COMMAND track_tests)
    # Detect regressions to nonterminating angle normalization loops.
    set_tests_properties(tracking PROPERTIES TIMEOUT 10)

    # Compile the real gate implementation separately for each tuning combination.
    function(add_gate_tests name range_m azimuth_deg)
        add_executable(gating_${name} src/gating_cases.c
            ../../src/tracking/gating.c ../../src/tracking/angle_utils.c)
        target_include_directories(gating_${name} PRIVATE ../../src ../common/include)
        target_compile_definitions(gating_${name} PRIVATE
            CONFIG_RADAR_RANGE_GATE_M=${range_m}
            CONFIG_RADAR_AZIMUTH_GATE_DEG=${azimuth_deg})
        target_compile_features(gating_${name} PRIVATE c_std_11)
        target_compile_options(gating_${name} PRIVATE -Wall -Wextra -Werror -UNDEBUG)
        target_link_libraries(gating_${name} PRIVATE m)
        foreach(case IN ITEMS range_boundary angle_boundary both_required wrapped_error)
            add_test(NAME gating_${name}_${case} COMMAND gating_${name} ${case})
        endforeach()
    endfunction()
    add_gate_tests(default 100 10)
    add_gate_tests(minimum 1 1)
    add_gate_tests(narrow 50 5)
    add_gate_tests(wide 250 30)
    add_gate_tests(maximum 65535 180)
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
