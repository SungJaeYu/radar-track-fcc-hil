# STATUS handler host tests

Run without a board or Zephyr installation:

```sh
cmake -G Ninja -S fcc_app/tests/test_frame_handler -B build_frame_handler \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build_frame_handler
ctest --test-dir build_frame_handler --output-on-failure
```

Ten cases exercise the real handler, status payload encoder, protocol encoder,
parser, and tracking code:

- Zero counters and one response submission per request.
- Distinct counter values, UINT32_MAX, and fresh values on repeated requests.
- Active track preservation and unchanged timeout arming/refresh behavior.
- Exact 16-byte big-endian payload and a fixed 21-byte frame/CRC reference,
  independently calculated with Python binascii; parsing the resulting frame.
- NULL input/output rejection with -EINVAL and unchanged output on failure.
- TX submission errors (-ENOMSG, -EINVAL, -EIO), no immediate retry, preserved
  track/counters, and successful submission on the next request.
- Injected encoder failure prevents TX submission; the next request recovers.
- An incoming STATUS_REPORT is ignored without replying or falling into default.

The fixture captures printk and copies requests at the uart_tx_submit boundary.
It supplies diagnostic snapshots and submission results; the encoder wrapper
only injects the otherwise unreachable failure for valid local pointers.
Successful calls and direct encoder tests use the actual production encoder.

These tests do not schedule RTOS threads or exercise the actual TX queue/UART.
Real counter updates are covered by test_uart_rx, while queue and byte emission
are covered separately by test_uart_tx. Board concurrency, physical delivery,
and PC/GUI decoding remain outside this suite.

2026-11-12: all ten cases and the firmware build passed. No flash or board test
was performed. Firmware source was reviewed, not modified by the test update.

The local VS Code task `Host: Configure STATUS tests` generates this test's own
compile database. The test must use host headers, not the firmware's Zephyr paths.

2026-11-04: all five STATUS cases and all 20 UART cases passed. The firmware
build also passed with the empty TX module registered. CMake regeneration
reported the existing board devicetree duplicate unit-address warning at
0x90000000; no new C compiler warnings occurred. No flash or hardware
validation was performed.


## 공통 host 실행 형식

`frame_handler_tests`를 인자 없이 실행하면 전체 사례를 실행합니다.
`frame_handler_tests test_name`으로 한 사례만 선택할 수 있습니다.
각 성공은 `PASS: test_name`으로 출력하며 잘못된 이름은 종료 코드 2를 반환합니다.
실행기는 `../common/test_runner.h`를 공유합니다. 모사 환경과 검증 범위는 각 suite가 유지합니다.
