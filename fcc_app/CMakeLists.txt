# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(fcc_app)

target_sources(app PRIVATE
    src/main.c
    src/transport/uart_rx.c
    src/protocol/frame_parser.c
    src/common/ring_buffer.c
    src/application/frame_handler.c
    src/protocol/radar_message.c
    src/tracking/kalman_cv.c
    src/tracking/gating.c
    src/tracking/angle_utils.c
    src/tracking/track.c
    src/transport/rx_frame_processor.c
    src/transport/uart_tx.c
    src/protocol/protocol_frame.c
    src/common/crc16.c
    src/protocol/status_message.c
)

# Resolve module-qualified includes from the application source root.
target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
