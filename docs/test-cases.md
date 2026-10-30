# 테스트 케이스 목록

현재 테스트 소스를 기준으로 작성한 목록입니다. 테스트를 추가하거나 검증 조건을 바꿀 때 이 문서도 함께 갱신합니다.
케이스 목록과 실제 실행 결과는 구분합니다. 아래 개수는 Python 수집 결과와 C 등록 코드를 기준으로 합니다.

## 구성과 개수

| 구분 | 위치 | 케이스 수 | 실행 환경 |
|---|---|---:|---|
| 수동 송신기·INIT/RESET | `tests/test_hil_protocol.py` | 6 | Python, serial 모사 |
| 프로토콜·단위 변환 | `tests/test_protocol.py` | 18 | Python |
| 연속 측정 송신기 | `tests/test_radar_sender.py` | 5 + 내부 subtest 9 | Python, serial·sleep 모사 |
| 시나리오 실행기 | `tests/test_scenarios.py` | 15 | Python CLI, serial 모사 |
| GUI 측정 모델 | `tests/test_gui_model.py` | 7 | Python |
| GUI 화면·통신 작업부 | `tests/test_gui.py` | 4 | offscreen Qt, serial 모사 |
| 필터·추적 | `fcc_app/tests/test_kalman` | 13 | host C 또는 Zephyr Ztest |
| UART drain·링버퍼 | `fcc_app/tests/test_uart_rx` | 8 | host C, Zephyr API 모사 |

Python은 매개변수 조합을 펼쳐 **55개 항목** (GUI 의존성 설치 시)을 수집합니다. unittest 내부 subtest 9개는 이 수집 항목과 별도로 표시될 수 있습니다.
Tracking C는 CTest에 `tracking` 하나로 등록되어 그 안에서 13개 함수를 실행합니다.
UART C는 각 함수를 CTest에 따로 등록하므로 8개로 표시됩니다.
`pc_sim/small_byte_test.py`는 이름에 test가 있지만 자동 테스트가 아닌 수동 송신 도구입니다.

## Python: 수동 송신기·INIT/RESET

소스: [test_hil_protocol.py](../tests/test_hil_protocol.py)

| 테스트 함수 | 수 | 입력·검증 내용 |
|---|---:|---|
| `test_init_frame_matches_firmware_wire_format` | 1 | TYPE=1, `hello` payload가 `7e 05 01 68 65 6c 6c 6f 63`으로 인코딩되는지 |
| `test_small_byte_sender_writes_init_frame` | 1 | 수동 송신기가 위 INIT 프레임을 모사 serial에 정확히 한 번 쓰는지 |
| `test_small_byte_rejects_invalid_byte_length` | 3 | 빈 문자열, ASCII 33글자, UTF-8 `가` 11글자(33바이트)를 종료 코드 2로 거부하고 포트를 열지 않는지 |
| `test_small_byte_sender_can_send_firmware_reset` | 1 | TYPE=5, payload `r`이 `7e 01 05 72 77`로 한 번 송신되는지 |

## Python: 프로토콜·단위 변환

소스: [test_protocol.py](../tests/test_protocol.py)

| 테스트 함수 | 수 | 입력·검증 내용 |
|---|---:|---|
| `test_init_wire_format` | 1 | 자동 INIT의 바이트가 `7e 01 01 00 01`인지 |
| `test_radar_wire_format` | 1 | 100ms, 1000m, semicircle -1073741824의 전체 프레임·XOR가 기대값과 같은지 |
| `test_reject_payload_lengths_firmware_cannot_parse` | 2 | payload 0바이트와 33바이트를 `ValueError`로 거부하는지 |
| `test_scenario_radians_to_wire` | 6 | 0, ±π/2, ±π, 3π를 signed int32 semicircle로 변환하고 1000.4m를 1000m로 반올림하는지; ±π와 3π는 -2147483648 |
| `test_scenario_rejects_unrepresentable_ranges` | 6 | 거리 0, 0.1, -1, 65536, inf, nan을 거부하는지 |
| `test_scenario_rejects_nonfinite_angles` | 2 | 방위각 inf, nan을 거부하는지 |

## Python: 연속 측정 송신기

소스: [test_radar_sender.py](../tests/test_radar_sender.py), 클래스 `RadarSenderTest`

| 테스트 함수 | 입력·검증 내용 |
|---|---|
| `test_serial_stream_and_interval` | INIT 후 (0ms, 1000m), (100ms, 998m)를 순서대로 송신하고 두 측정 사이에 0.1초 sleep을 한 번 호출하는지 |
| `test_big_endian_signed_azimuth_and_checksum` | 음수 semicircle의 big-endian 인코딩과 전체 XOR 프레임이 기대값과 같은지 |
| `test_semicircle_boundaries` | subtest 3개: int32 최솟값, 0, 최댓값의 방위각 바이트와 payload 길이 10 확인 |
| `test_cli_accepts_semicircle` | CLI에서 -1073741824를 받아 INIT을 먼저 출력하고 측정에 `c0 00 00 00`을 포함하는지 |
| `test_invalid_settings` | subtest 6개: hz=0/nan, count=0, noise=-1, int32 범위 밖 방위각 두 값을 종료 코드 2로 거부하는지 |

sleep 호출 검사는 실제 운영체제의 송신 주기 정확도를 보장하지 않습니다.

## Python: 시나리오 실행기

소스: [test_scenarios.py](../tests/test_scenarios.py)

| 테스트 함수 | 수 | 입력·검증 내용 |
|---|---:|---|
| `test_single_target_cli_emits_current_frames` | 2 | 모듈 실행과 파일 직접 실행 모두 INIT + 측정 2개 출력; (0ms, 30000m, 0), (100ms, 29985m, 0), 프레임 길이 14와 XOR 확인 |
| `test_multitarget_generation_remains_available_without_board` | 1 | 보드 없는 four_targets 출력에 INIT을 포함한 프레임 9개가 나오는지 |
| `test_serial_rejects_unsupported_multimeasurement_scans` | 2 | UART 모드에서 two_crossing 또는 clutter=1을 종료 코드 2와 single target 오류로 거부하는지 |
| `test_missed_detections_emit_no_measurement` | 1 | 탐지확률 0이면 INIT만 출력하고 측정은 생성하지 않는지 |
| `test_invalid_scenario_settings` | 8 | dt=0/0.0001, duration=nan, pd=1.1, clutter=-1, noise=-1, azimuth-noise=inf, baud=0을 오류로 종료하는지 |
| `test_uart_scenario_sends_current_frames_and_closes` | 1 | 모사 serial에 INIT + 측정 2개를 정확한 바이트로 쓰고 context manager를 종료하는지 |

## C: 필터·추적

소스: [test_kalman/src/main.c](../fcc_app/tests/test_kalman/src/main.c)

| 테스트 함수 | 입력·검증 내용 |
|---|---|
| `test_angle_wrap` | ±181°를 ∓179°로 wrap, -π를 π로 정규화, ±179° 사이의 최단 각도 차이가 ±2°인지 |
| `test_gate` | 거리 차이 +100m는 통과, ±101m는 거부; 각도 9°는 통과, ±11°는 거부; ±179° 경계를 가로지르는 측정은 통과 |
| `test_cv_prediction_and_covariance` | dt=0.5초 예측값·변화율과 수작업 계산한 FPFᵀ+Q 공분산이 같은지 |
| `test_cv_correction` | 측정 14로 보정한 추정값 12, 변화율 2.5 및 공분산이 기대값과 같은지 |
| `test_angle_prediction_and_correction` | 179°에서 예측·보정 시 ±180° 경계를 올바르게 처리하는지 |
| `test_stationary_convergence` | 1000m 정지 측정을 200회 처리해 거리·속도가 수렴하고 공분산 대각이 음수가 아니며 대칭이 유지되는지 |
| `test_decode_semicircle` | 10바이트 payload의 시간·거리·±π/2·int32 각도 경계 해석; 잘못된 길이와 NULL 인자 거부; 거리 0 거부 |
| `test_timestamp_rejection` | 중복, 역행, 정확히 2³¹ms 차이의 timestamp를 거부하고 트랙 메모리를 변경하지 않는지 |
| `test_elapsed_time_and_wraparound` | 300ms 간격의 예측과 uint32 timestamp 순환을 넘는 100ms 간격 계산 확인 |
| `test_long_gap_and_restart` | 간격 1000ms는 허용, 1001ms는 EMPTY로 초기화; 새 측정으로 TENTATIVE·필터 재시작 |
| `test_confirmation_coasting_and_loss` | 최초 측정 포함 3 hit로 CONFIRMED; miss 후 COASTING, 재검출로 CONFIRMED; 연속 5 miss로 LOST |
| `test_tentative_miss_and_inactive_calls` | TENTATIVE에서 miss 시 EMPTY; EMPTY에서 hit/miss/predict 호출이 상태를 변경하지 않는지 |
| `test_lost_restarts_for_any_timestamp` | LOST에서 hit/miss는 상태 유지; timestamp 0/900/1000으로 predict 시 EMPTY, 이후 새 트랙 시작 가능 |

실제 `frame_handler.c`는 이 테스트에 포함되지 않습니다. 송신 timestamp 간격 검사와 보드 uptime 기반 무수신 timeout은 서로 다른 기능입니다.

## C: UART drain·링버퍼

소스: [test_uart_rx/src/main.c](../fcc_app/tests/test_uart_rx/src/main.c)

| 테스트 함수 | 입력·검증 내용 |
|---|---|
| `test_dispatch_drains_complete_frame` | 실제 ISR에 용량을 초과하는 입력을 넣고 복구 dispatcher 호출; 저장된 정상 INIT 1개 처리 후 링·파서·overflow 초기화 |
| `test_preserves_partial_parser_and_drops_new_input` | 기존 파서의 레이더 앞 7바이트와 링의 뒤 7바이트로 프레임 완성; handler 중 새 INIT을 주입해 링에 저장되지 않는지 |
| `test_discards_incomplete_frame` | 버퍼 끝의 레이더 앞 7바이트만으로는 handler를 호출하지 않고 복구 후 부분 프레임 폐기 |
| `test_rejects_bad_checksum` | 잘못된 XOR를 가진 INIT을 handler로 전달하지 않는지 |
| `test_receives_after_recovery` | 부분 프레임 폐기·복구 후 새 INIT을 정상 수신·파싱하는지 |
| `test_empty_recovery` | 빈 링에 overflow 플래그만 설정해도 안전하게 초기화되는지 |
| `test_ring_overflow_preserves_buffer` | 용량까지 채운 후 push 실패 시 head/tail과 기존 바이트 순서를 보존하는지 |
| `test_ring_wraparound` | 초기화 없이 채우기·비우기 3회 반복; 인덱스 순환 후에도 바이트 순서와 full/empty 판정 유지 |

현재 BUFFER_SIZE=256, 실제 용량=255이며 fixture는 버퍼 크기에서 입력 길이를 계산합니다.
현재 선택된 **drain 정책**을 검증하므로 discard-all 정책으로 변경하면 일부 기대값이 맞지 않습니다.
실제 ISR·복구·링버퍼·파서를 사용하지만 UART FIFO, IRQ, atomic, 시간, handler는 모사합니다.
무한 RX 스레드는 실행하지 않으며 실제 동시성·스케줄링·무수신 timeout 의미는 검증하지 않습니다.

## GUI 테스트

GUI 의존성 설치·실행법은 [GUI 안내](../pc_sim/GUI.md)를 참고합니다.
`test_gui.py`는 PySide6/pyqtgraph가 없으면 모듈 전체를 skip합니다.

| 테스트 함수 | 수 | 검증 내용 |
|---|---:|---|
| `test_gui_sample_matches_wire_and_motion` | 1 | 200ms 시점의 접근 거리 996m, -90°의 signed semicircle과 실제 wire payload |
| `test_gui_rejects_invalid_settings` | 5 | hz=0, noise=-1, range=nan, duration=0, azimuth=181° 거부 |
| `test_gui_stops_out_of_range_instead_of_wrapping` | 1 | 접근 중 거리 범위 이탈 시 잘못된 정수로 순환하지 않고 오류 반환 |
| `test_gui_preview_start_stop_restart_and_close` | 1 | 실제 Qt 화면의 연결·시작·중지·재시작·그래프 갱신·실행 중 창 닫기 |
| `test_gui_worker_init_restart_and_serial_cleanup` | 1 | 실행마다 INIT 선행, 중지 후 재시작 순서, 스레드 종료 시 포트 해제 |
| `test_gui_worker_write_failure_closes_port` | 1 | serial write 실패 시 오류 전달·스레드 종료·포트 해제 |
| `test_gui_same_port_rejected_before_open` | 1 | 데이터·콘솔에 동일 포트 지정 시 실제 포트를 열기 전에 거부 |

## 실행 방법

저장소 루트에서 실행합니다. Python 의존성이 설치된 Zephyr venv를 사용합니다.

```sh
source ~/zephyrproject/.venv/bin/activate
python -m pytest
python -m pytest --collect-only -q

cmake -G Ninja -S fcc_app/tests/test_kalman -B /tmp/radar-track-tests -DTRACK_TEST_HOST=ON
cmake --build /tmp/radar-track-tests
ctest --test-dir /tmp/radar-track-tests --output-on-failure

cmake -G Ninja -S fcc_app/tests/test_uart_rx -B /tmp/radar-uart-tests
cmake --build /tmp/radar-uart-tests
ctest --test-dir /tmp/radar-uart-tests --output-on-failure
```

개별 실행 예시:

```sh
python -m pytest tests/test_protocol.py -q
ctest --test-dir /tmp/radar-uart-tests -R '^test_ring_wraparound$' --output-on-failure
/tmp/radar-track-tests/track_tests
```

마지막 명령은 Tracking 13개 전체를 실행하며 개별 이름을 출력합니다.
macOS 컴파일러·SDK 지정 방법과 모사 범위는 [UART 안내](../fcc_app/tests/test_uart_rx/README.md),
Zephyr Ztest 실행법은 [Tracking 안내](../fcc_app/tests/test_kalman/README.md)를 참고합니다.

## 실제 보드 시험과 남은 검증

이번 세션에서 현재 소스를 빌드·플래시한 뒤 수행한 수동 smoke 시험은 자동 테스트 개수에 포함하지 않습니다.
INIT 1개와 잡음 없는 측정 5개를 5Hz로 보내 콘솔에서 INIT 1회, 측정 5회,
필터 초기화 후 보정 4회, 송신 중단 후 timeout 1회를 확인했습니다.
이는 PC → USART6 RX와 ST-Link 콘솔 경로의 기본 동작 확인입니다.

아래 항목은 현재 자동 테스트 또는 해당 smoke 시험에서 확인하지 않았습니다.

- 실제 보드에서 강제 overflow 후 정상 수신 복귀, 손실량·IRQ 타이밍.
- USART6 TX(PC6)의 보드 → PC 송신 경로.
- 첫 정상 측정 전 timeout 비활성, INIT/RESET 직후 timeout 해제의 독립적인 자동 회귀 검사.
- 실제 RX 스레드 스케줄링과 UART 하드웨어 overrun.
- discard-all 정책의 전용 회귀 테스트와 파서 전체 상태·오류 조합.
- 장시간 운전, 부하별 수신률, 추적 결과 반환을 통한 폐루프 오차 측정.

추가 보드 시험은 [보드 검증 절차](board-bringup.md)를 따릅니다.
