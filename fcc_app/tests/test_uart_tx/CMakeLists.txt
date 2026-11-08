cmake_minimum_required(VERSION 3.20.0)
project(test_uart_tx C)
enable_testing()
add_executable(uart_tx_tests src/main.c ../../src/protocol_frame.c
    ../../src/crc16.c ../../src/frame_parser.c)
target_include_directories(uart_tx_tests PRIVATE include ../../src)
target_compile_features(uart_tx_tests PRIVATE c_std_11)
target_compile_options(uart_tx_tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)
foreach(test_name IN ITEMS test_tx_init test_tx_invalid_submit
        test_tx_queue_copy_fifo_full_and_recovery test_tx_encode_error_continues)
    add_test(NAME ${test_name} COMMAND uart_tx_tests ${test_name})
endforeach()
