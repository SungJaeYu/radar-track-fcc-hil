# UART TX host tests

```sh
cmake -G Ninja -S fcc_app/tests/test_uart_tx -B build_uart_tx \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build_uart_tx
ctest --test-dir build_uart_tx --output-on-failure
```

Four cases compile the actual TX worker, encoder, CRC, and parser. They check
initialization success/failure, invalid requests, copied queue entries, FIFO
order, queue saturation and recovery, and continuation after encoding failure.
Known INIT bytes are checked independently of the encoder; the Python CRC suite
also compares C encoder output against Python for minimum/maximum payloads.

The test doubles simulate queue storage, readiness, and UART byte output.
The real worker runs synchronously until the queue becomes empty; `longjmp`
returns to the test at the point where the worker would block. No threads are
scheduled. These tests do not prove semaphore blocking, IRQ behavior, Zephyr
queue concurrency, UART timing, or physical delivery. STATUS submission is covered separately by test_frame_handler. PC response
handling and physical STATUS delivery remain unverified.

2026-11-08: all four cases passed. Firmware incremental build passed without
warnings. No board flash or HIL was performed.


## 공통 host 실행 형식

`uart_tx_tests`를 인자 없이 실행하면 전체 사례를 실행합니다.
`uart_tx_tests test_name`으로 한 사례만 선택할 수 있습니다.
각 성공은 `PASS: test_name`으로 출력하며 잘못된 이름은 종료 코드 2를 반환합니다.
실행기는 `../common/test_runner.h`를 공유합니다. 모사 환경과 검증 범위는 각 suite가 유지합니다.
