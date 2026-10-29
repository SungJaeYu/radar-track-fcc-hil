# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(fcc_app)

target_sources(app PRIVATE
    src/main.c
    src/uart_rx.c
    src/frame_parser.c
    src/ring_buffer.c
    src/frame_handler.c
    src/radar_message.c
    src/kalman_cv.c
    src/gating.c
    src/angle_utils.c
    src/track.c
)
