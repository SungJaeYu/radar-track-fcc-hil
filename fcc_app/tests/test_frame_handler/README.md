# STATUS handler host tests

Run without a board or Zephyr installation:

```sh
cmake -G Ninja -S fcc_app/tests/test_frame_handler -B build_frame_handler \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build_frame_handler
ctest --test-dir build_frame_handler --output-on-failure
```

Five cases exercise the real handler and tracking code: zero counters, distinct
counter values including UINT32_MAX and repeated reads, unchanged active track,
no timeout before the first measurement, and no timeout refresh from STATUS.

The fixture captures printk output and supplies a stats snapshot. Real counter
updates and parser CRC/length rejection are covered by `test_uart_rx`. The RTOS
stubs do not schedule threads or exercise the queue. These tests do not validate
atomic concurrency, UART TX, or board behavior. STATUS currently prints a log;
it does not send a response frame to the PC.

The local VS Code task `Host: Configure STATUS tests` generates this test's own
compile database. The test must use host headers, not the firmware's Zephyr paths.

2026-11-04: all five STATUS cases and all 20 UART cases passed. The firmware
build also passed with the empty TX module registered. CMake regeneration
reported the existing board devicetree duplicate unit-address warning at
0x90000000; no new C compiler warnings occurred. No flash or hardware
validation was performed.
