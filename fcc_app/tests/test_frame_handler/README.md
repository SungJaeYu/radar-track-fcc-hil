# Frame handler host tests

Run without a board or Zephyr installation:

```sh
cmake -G Ninja -S fcc_app/tests/test_frame_handler -B build_frame_handler \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build_frame_handler
ctest --test-dir build_frame_handler --output-on-failure
```

Thirty-one cases exercise the real handler, STATUS/TRACK payload encoders, protocol encoder,
parser, and tracking code:

- Zero counters and one response submission per request.
- Distinct counter values, UINT32_MAX, and fresh values on repeated requests.
- Active track preservation and unchanged timeout arming/refresh behavior.
- Exact 32-byte big-endian payload and a fixed 37-byte frame/CRC reference,
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

The CMake configure command above generates this test's own compile database.
The test must use host headers, not the firmware's Zephyr paths.

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


## Measurement and control coverage

Seven additional cases enter through frame_handler_process(), using distinct sender and board
receive timestamps. They check INIT/RESET, first valid measurement arming, refresh,
exact timeout boundaries, one-shot expiry, invalid payload rejection, and receive
refresh after gate/timestamp rejection. No production firmware source was changed.
Control cases compare STATUS payloads before/after reset using supplied snapshots;
real RX atomic counters and handler commands are not integrated in this fixture.

## Timeout configuration

Host CMake cache variable `RADAR_MEASUREMENT_TIMEOUT_MS` defaults to 1000 ms and supplies
`CONFIG_RADAR_MEASUREMENT_TIMEOUT_MS` to the tested source. Use a separate build directory
and add `-DRADAR_MEASUREMENT_TIMEOUT_MS=2300` to the configure command to test a non-default value.
Host configuration is independent of Zephyr prj.conf.

STATUS combines four processor counters, three UART counters, and the real handler stale counter. RX owners are mocked;
real counter increments are tested separately by the UART suite.

로그 정리 회귀: STATUS 성공은 큐 접수 한 줄이며 실패 시 성공 로그를 내지 않는다.
기존 트랙의 정상 갱신은 입력·예측·결과 3줄과 TRACK 큐 접수 1줄을 확인한다. 상태 변화 시 이름을 기록하고 상태가 같으면 전이 로그를 내지 않는다.
host 모사는 DEBUG도 캡처하므로 실제 필터링·deferred 출력 검증은 별도다.

최신 로깅 일반·디버그 실보드 회귀는 [별도 시험 기록](../../../docs/hardware-test-2026-11-14-logging.md)을 참고합니다.
host 테스트의 로그 캡처와 실제 보드의 지연 출력 검증은 구분합니다.

## TRACK_REPORT host coverage (2026-11-16)

Ten additional cases verify the current measurement-report path:

| Case | Evidence |
|---|---|
| `test_track_encode_wire` | Fixed Python-derived 37-byte frame/CRC, float positive/negative/fraction/negative-zero values, all offsets, guards and parser round trip |
| `test_track_encode_invalid_arguments` | NULL rejection and unchanged output |
| `test_track_started_updated_and_confirmed` | One report per valid measurement, corrected estimate, state/result/flags, distinct sender and board time |
| `test_track_timestamp_rejection_report` | Duplicate/backward input retains estimate timestamp while echoing the rejected input timestamp |
| `test_track_gate_rejection_empty_and_lost` | TENTATIVE rejection clears validity; COASTING remains valid; LOST clears values; next input resets and following input restarts |
| `test_track_gap_reset_and_recovery` | Gap over 1000ms clears validity and next input restarts |
| `test_track_sequence_wrap_and_timeout_preservation` | Sequence and sender timestamp wrap, timeout preserves sequence, restart continues it |
| `test_track_submit_failure_and_recovery` | Queue-full/invalid/I/O failures consume sequence without immediate retry; next report succeeds |
| `test_track_encode_failure_and_recovery` | Injected encoding failure consumes sequence without submission; next report succeeds |
| `test_incoming_track_report_is_ignored` | Unexpected inbound report changes no handler state and causes no response |

Existing INIT/RESET tests now verify sequence preservation and continued numbering;
invalid measurements neither submit reports nor consume sequence numbers. Measurement
log expectations include the new TX queue acceptance line. Encoder failures are injected
only at the handler boundary; direct encoding tests call production code.

All 29 cases pass. INIT/RESET/timeout event reports are not implemented or certified
by these tests. PC TRACK decoding, GUI evaluation, actual UART delivery and queue
scheduling remain outside this host suite. No firmware behavior was changed for these tests.

Report composition and submission now live in `application/report_sender.c`;
`frame_handler` owns its sender state and preserves it across resets. The fixture
includes the real sender with encoder failure injection, then the real handler;
wire/sequence/invalid-estimate expectations are unchanged by this extraction.
The sender shares the handler logging module and adds no thread or queue.

Radar processing now lives in a separately compiled `application/radar_processor.c`.
The handler owns `radar` and `report_sender` states and connects processing to submission.
Two processor API cases additionally check invalid-input output/state preservation
and repeated timeout queries with no state change, logging, submission, or sequence consumption:
`test_processor_invalid_input_preserves_output` and
`test_processor_timeout_query_preserves_state`.
Integration expectations include one no-input TRACK report for INIT/RESET and timeout.

2026-11-18: timeout querying is side-effect-free and accepts a const processor.
The handler owns reset, logging, and one no-input TRACK report per expiration.
The 29 host cases pass, including repeated-query state preservation, repeated
handler checks without duplicate reports, and INIT/RESET report sequence use.
No firmware build, flash, or hardware test was performed for this change.

## Stale queue policy (2026-11-18)

Current total: 30 cases. `test_stale_measurement_boundary_and_stats` controls the
mock uptime and verifies age timeout-1 acceptance, exact-boundary rejection,
unchanged radar state and sequence, subsequent one-shot timeout, first-measurement
arming suppression, reset preservation, UINT32_MAX saturation and fresh recovery.
Age is MCU processing uptime minus RX completion uptime, not sender time.
STATUS now carries eight uint32 fields (32 payload / 37 wire bytes), including the
handler-owned stale count at offset 28. RX snapshots remain mocked.
Earlier dated counts describe earlier revisions; hardware queue delay injection
is not covered by these synchronous tests.

## Optional performance instrumentation

Configure a separate build with `-DPERF_TEST_ENABLED=ON` to link the real collector and exercise the production hooks. The normal host build keeps instrumentation disabled. See [performance test commands and cases](../test_perf_monitor/README.md). The handler variant adds a measurement-path case; the TX variant adds counter assertions to its existing cases. These host checks do not measure board performance.


## 순간 이상치 회귀 — 2026-11-19

`test_track_spike_recovery_matrix`를 추가했습니다. 정상40표본 뒤 거리/각도24값을1회 또는5회 주입한48조합에서 첫 게이트 결과·LOST·정상 복귀·순번/입력 시각을 확인합니다. 거리1m/65535m, 게이트 안·경계·밖과 각도±180°를 포함합니다. 회복은 CONFIRMED·UPDATED·거리2m/각도0.2° 이내3표본 연속이며, host 모사 시간축에서 실행합니다. 일반31개·perf 활성화32개 함수가 등록돼 있습니다. [실보드 실행](../../../pc_sim/README.md#순간-이상치정상-복귀-실보드-시험)은 별도로 수행합니다.

## Prediction policy ownership — 2026-11-21

`radar_processor` checks lifecycle and sensor timestamps before calling the pure
next-track predictor. Existing cases now also cover half-range timestamp rejection
with unchanged track bytes, the 1000/1001 ms boundary, and LOST reset priority for
zero, backward, duplicate, forward, excessive-gap, and half-range timestamps.
Reset measurements are not reused for startup; the next measurement starts a track.
