# 테스트 케이스 목록

각 사례의 요구사항·상세 규격 연결과 미검증 항목은 [요구사항 추적표](requirements-traceability.md)를 참고합니다.

현재 테스트 소스를 기준으로 작성한 목록입니다. 테스트를 추가하거나 검증 조건을 바꿀 때 이 문서도 함께 갱신합니다.
현재 합계는 Python 136개 + subtest 9개, C host 97회 실행(RX drain 21·discard 18·handler 17·TX 4·Tracking 17·게이팅 설정 20)입니다.
아래 날짜별 기록의 옛 개수는 당시 결과로 보존합니다.
케이스 목록과 실제 실행 결과는 구분합니다. 아래 개수는 Python 수집 결과와 C 등록 코드를 기준으로 합니다.

## 구성과 개수

| 구분 | 위치 | 케이스 수 | 실행 환경 |
|---|---|---:|---|
| 수동 송신기·INIT/RESET | `tests/test_hil_protocol.py` | 6 | Python, serial 모사 |
| PC STATUS CLI | `tests/test_status_client.py` | 21 | Python, serial·monotonic clock 모사 |
| PC 수신 프레임 파서 | `tests/test_frame_parser.py` | 16 | Python, serial I/O 없음 |
| 프로토콜·단위 변환 | `tests/test_protocol.py` | 23 | Python |
| 연속 측정 송신기 | `tests/test_radar_sender.py` | 5 + 내부 subtest 9 | Python, serial·sleep 모사 |
| 시나리오 실행기 | `tests/test_scenarios.py` | 15 | Python CLI, serial 모사 |
| GUI 측정 모델 | `tests/test_gui_model.py` | 7 | Python |
| GUI 화면·통신 작업부 | `tests/test_gui.py` | 9 | offscreen Qt, serial 모사 |
| CRC·인코더·펌웨어 상호운용 | `tests/test_crc_interop.py` | 28 | Python + 실제 C 파서 host 컴파일 |
| 내부 assert | `tests/test_firmware_assertions.py` | 6 | 실제 C host 자식 프로세스의 SIGABRT·메시지 검증 |
| 게이팅 설정 행렬 | `fcc_app/tests/test_kalman/src/gating_cases.c` | 20 (5 설정 × 4 사례) | host C |
| 필터·추적 | `fcc_app/tests/test_kalman` | 17 | host C 또는 Zephyr Ztest |
| TX 큐·송신 | `fcc_app/tests/test_uart_tx` | 4 | host C, UART·큐·세마포어 모사 |
| STATUS 처리 | `fcc_app/tests/test_frame_handler` | 17 | host C, 통계 조회·TX 제출·printk·RTOS 모사 |
| UART drain·링버퍼 | `fcc_app/tests/test_uart_rx` | 39 (drain 21 + discard 18) | host C, Zephyr API 모사 |

Python은 매개변수 조합을 펼쳐 **136개 항목** (GUI 의존성 설치 시)을 수집합니다. unittest 내부 subtest 9개는 이 수집 항목과 별도로 표시될 수 있습니다.
Tracking C는 CTest의 `tracking`에서 17개 함수를 실행하며, 게이팅 설정 시험 20개가 별도로 등록됩니다.
UART C는 정책별 실행을 CTest에 따로 등록하므로 39개로 표시됩니다. 공통 사례 16개는 두 번 실행합니다.
`pc_sim/small_byte_test.py`는 이름에 test가 있지만 자동 테스트가 아닌 수동 송신 도구입니다.

## Python: 수동 송신기·INIT/RESET

소스: [test_hil_protocol.py](../tests/test_hil_protocol.py)

| 테스트 함수 | 수 | 입력·검증 내용 |
|---|---:|---|
| `test_init_frame_matches_firmware_wire_format` | 1 | TYPE=1, `hello` payload가 `7e 05 01 68 65 6c 6c 6f 0e ab`으로 인코딩되는지 |
| `test_small_byte_sender_writes_init_frame` | 1 | 수동 송신기가 위 INIT 프레임을 모사 serial에 정확히 한 번 쓰는지 |
| `test_small_byte_rejects_invalid_byte_length` | 3 | 빈 문자열, ASCII 33글자, UTF-8 `가` 11글자(33바이트)를 종료 코드 2로 거부하고 포트를 열지 않는지 |
| `test_small_byte_sender_can_send_firmware_reset` | 1 | TYPE=5, payload `r`이 `7e 01 05 72 5a 8c`로 한 번 송신되는지 |

## Python: 프로토콜·단위 변환

소스: [test_protocol.py](../tests/test_protocol.py)

| 테스트 함수 | 수 | 입력·검증 내용 |
|---|---:|---|
| `test_init_wire_format` | 1 | 자동 INIT의 바이트가 `7e 01 01 00 c8 9d`인지 |
| `test_radar_wire_format` | 1 | 100ms, 1000m, semicircle -1073741824의 전체 프레임·CRC-16가 기대값과 같은지 |
| `test_reject_payload_lengths_firmware_cannot_parse` | 2 | payload 0바이트와 33바이트를 `ValueError`로 거부하는지 |
| `test_scenario_radians_to_wire` | 6 | 0, ±π/2, ±π, 3π를 signed int32 semicircle로 변환하고 1000.4m를 1000m로 반올림하는지; ±π와 3π는 -2147483648 |
| `test_scenario_rejects_unrepresentable_ranges` | 6 | 거리 0, 0.1, -1, 65536, inf, nan을 거부하는지 |
| `test_scenario_rejects_nonfinite_angles` | 2 | 방위각 inf, nan을 거부하는지 |

## Python: 연속 측정 송신기

소스: [test_radar_sender.py](../tests/test_radar_sender.py), 클래스 `RadarSenderTest`

| 테스트 함수 | 입력·검증 내용 |
|---|---|
| `test_serial_stream_and_interval` | INIT 후 (0ms, 1000m), (100ms, 998m)를 순서대로 송신하고 두 측정 사이에 0.1초 sleep을 한 번 호출하는지 |
| `test_big_endian_signed_azimuth_and_checksum` | 음수 semicircle의 big-endian 인코딩과 전체 CRC-16 프레임이 기대값과 같은지 |
| `test_semicircle_boundaries` | subtest 3개: int32 최솟값, 0, 최댓값의 방위각 바이트와 payload 길이 10 확인 |
| `test_cli_accepts_semicircle` | CLI에서 -1073741824를 받아 INIT을 먼저 출력하고 측정에 `c0 00 00 00`을 포함하는지 |
| `test_invalid_settings` | subtest 6개: hz=0/nan, count=0, noise=-1, int32 범위 밖 방위각 두 값을 종료 코드 2로 거부하는지 |

sleep 호출 검사는 실제 운영체제의 송신 주기 정확도를 보장하지 않습니다.

## Python: 시나리오 실행기

소스: [test_scenarios.py](../tests/test_scenarios.py)

| 테스트 함수 | 수 | 입력·검증 내용 |
|---|---:|---|
| `test_single_target_cli_emits_current_frames` | 2 | 모듈 실행과 파일 직접 실행 모두 INIT + 측정 2개 출력; (0ms, 30000m, 0), (100ms, 29985m, 0), 프레임 길이 15와 CRC-16 확인 |
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
| `test_preserves_partial_parser_and_drops_new_input` | 기존 파서의 레이더 앞 7바이트와 링의 뒤 8바이트로 프레임 완성; handler 중 새 INIT을 주입해 링에 저장되지 않는지 |
| `test_discards_incomplete_frame` | 버퍼 끝의 레이더 앞 7바이트만으로는 handler를 호출하지 않고 복구 후 부분 프레임 폐기 |
| `test_rejects_bad_crc` | 잘못된 CRC를 가진 INIT을 handler로 전달하지 않는지 |
| `test_drain_rejects_bad_length_then_handles_valid_frame` | 길이 오류를 handler로 전달하지 않고 바로 이어지는 정상 INIT만 한 번 처리 |
| `test_receives_after_recovery` | 부분 프레임 폐기·복구 후 새 INIT을 정상 수신·파싱하는지 |
| `test_empty_recovery` | 빈 링에 overflow 플래그만 설정해도 안전하게 초기화되는지 |
| `test_ring_overflow_preserves_buffer` | 용량까지 채운 후 push 실패 시 head/tail과 기존 바이트 순서를 보존하는지 |
| `test_processor_copies_frame_and_reset_preserves_drops` | 프레임 복사와 reset 시 누적 드롭 보존 |
| `test_nonready_results_do_not_submit` | 미완성·CRC 오류·길이 오류 미제출 |
| `test_queue_full_counts_drop_and_recovers` | 큐 실패 집계와 다음 제출 재개 |
| `test_drain_queue_full_still_finishes_recovery` | 큐 실패에도 drain 복구 완료 |
| `test_ring_wraparound` | 초기화 없이 채우기·비우기 3회 반복; 인덱스 순환 후에도 바이트 순서와 full/empty 판정 유지 |
| `test_stats_snapshot_and_reset` | CRC·길이 오류, timeout, queue drop 집계와 조회·reset 후 누적값 보존 |
| `test_timeout_boundary_for_every_partial_frame` | 모든 불완전 프레임 분할 지점: 만료 직전·경계값·중복 집계 방지 |
| `test_timeout_ignores_idle_and_complete_frames` | 대기 상태와 정상 프레임 완료 후 timeout 제외 |
| `test_timeout_recovers_next_valid_frame` | 만료 후 다음 정상 프레임 내용·시각 보존 |
| `test_timeout_preserves_split_frames` | 모든 분할 지점의 제한 시간 이내 정상 수신 |
| `test_timeout_refreshes_on_each_byte` | 바이트별 시각 갱신: 전체 조립 시간 제한과 구분 |
| `test_explicit_reset_cancels_partial_timeout` | 명시적 reset 이후 만료 취소와 누적 카운터 보존 |

현재 BUFFER_SIZE=256, 실제 용량=255이며 fixture는 버퍼 크기에서 입력 길이를 계산합니다.
DRAIN·DISCARD를 별도 실행 파일로 컴파일합니다. 공통 16개와 정책별 전용 사례를 실행합니다.
실제 ISR·복구·링버퍼·파서를 사용하지만 UART FIFO, IRQ, atomic, 시간, frame_handler_submit은 모사합니다.
무한 RX 스레드는 실행하지 않으며 실제 동시성·스케줄링과 트랙 무수신 timeout은 검증하지 않습니다.
부분 프레임 timeout은 제어 가능한 모사 시각으로 실제 processor 함수를 검사합니다.

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
| `test_gui_settings_remain_accessible_in_short_window` | 1 | 1000×650 창에서 탭 전환 후 입력칸 최소 높이·비겹침, 마지막 항목 스크롤 접근, 고정 시작·중지 버튼과 세로 그래프·우측 전체 높이 콘솔 배치 확인 |
| `test_gui_same_port_rejected_before_open` | 1 | 데이터·콘솔에 동일 포트 지정 시 실제 포트를 열기 전에 거부 |

## CRC와 실제 펌웨어 파서 상호운용

소스: [test_crc_interop.py](../tests/test_crc_interop.py). 실제 `frame_parser.c`를 임시 공유 라이브러리로 컴파일합니다.
호스트 C 컴파일러가 없으면 C 상호운용 사례는 skip됩니다. 컴파일 오류는 실패로 보고합니다.

| 함수 | 수 | 검증 내용 |
|---|---:|---|
| `test_crc_standard_check_vector` | 1 | Python CRC의 `123456789` → `0x29B1` |
| `test_crc_matches_independent_reference` | 3 | 최소·최대 payload와 특수 바이트를 Python 표준 라이브러리 CRC와 비교 |
| `test_firmware_crc_standard_check_vector` | 1 | 실제 펌웨어 CRC 함수에 동일한 기준 벡터 적용 |
| `test_python_frames_accepted_by_firmware` | 4 | INIT·측정·RESET·최대 payload를 C 파서가 READY로 반환하며 type·payload 보존 |
| `test_corrupted_frame_rejected_then_next_frame_accepted` | 4 | TYPE·payload·CRC 상위·하위 변조 거부 후 정상 INIT 수신 |
| `test_invalid_length_rejected_then_next_frame_accepted` | 3 | LEN 0/33/255는 LENGTH_ERROR, 이후 정상 INIT 수신 |
| `test_crc_low_byte_required_for_completion` | 1 | CRC 하위 바이트 전까지 INCOMPLETE, 마지막 바이트 후 READY |
| `test_plausible_corrupted_length_does_not_report_ready` | 1 | LEN 10→9 변조가 READY로 처리되지 않고 CRC_ERROR 발생 |

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

2026-11-01 최신 CRC·큐·수신 모듈 분리본의 실제 GUI와 보드 시험을 완료했습니다.
정상 측정 20개, timeout, CRC·길이 오류 후 정상 복귀, RESET, burst 후 복귀를 확인했습니다.
GUI 연결·송신·중지·재시작·재연결도 확인했습니다.
[결과와 검증 한계](hardware-test-2026-11-01.md)를 참고하세요.

현재 남은 검증 항목은 다음과 같습니다. 이후 STATUS·정책별 host 검증으로 완료한 항목은 제외했습니다.

- 실제 보드에서 강제 overflow 후 정상 수신 복귀, 손실량·IRQ 타이밍.
- 실제 RX 스레드 스케줄링과 UART 하드웨어 overrun.
- 파서 전체 상태·오류 조합의 추가 검증.
- 장시간 운전, 부하별 수신률, 추적 결과 반환을 통한 폐루프 오차 측정.

추가 보드 시험은 [보드 검증 절차](board-bringup.md)를 따릅니다.

2026-11-03: 부분 프레임 timeout host 사례 6개를 추가했습니다. 모사 uptime을 직접
진행시키며 검사합니다. 이 host 시험과 별도로 `2aa57d9` 실보드에서 부분 프레임 중단 후 timeout 집계·정상 STATUS 복귀를 확인했습니다. 모든 경계·스케줄링 지연의 보드 검증을 뜻하지 않습니다.


## STATUS 처리 C 테스트

실제 handler·STATUS 인코더·프레임 인코더/파서·추적 코드를 실행하고 통계 조회값, TX 제출 결과와 `printk`를 모사합니다.
실행 방법: [STATUS 테스트 안내](../fcc_app/tests/test_frame_handler/README.md).

| 테스트 함수 | 검증 내용 |
|---|---|
| `test_status_zero` | 0인 네 통계값, 조회 1회, 16바이트 STATUS_REPORT 제출 1회 |
| `test_status_snapshot` | 서로 다른 값·UINT32_MAX 출력, 반복 조회 시 최신값 반영·누적값 보존 |
| `test_status_preserves_track` | 활성 트랙과 timeout 관리 상태를 변경하지 않음 |
| `test_status_does_not_arm_timeout` | 첫 측정 전 STATUS로 timeout 감시가 시작되지 않음 |
| `test_status_does_not_refresh_timeout` | STATUS 수신 후에도 마지막 측정 기준 1000 ms 경계에서 만료 |
| `test_status_encode_wire` | big-endian 필드 순서·경계값·독립 CRC 기준 프레임·출력 경계·실제 파서 수용 |
| `test_status_encode_invalid_arguments` | NULL 조합의 -EINVAL와 실패 시 출력 보존 |
| `test_status_submit_failure_and_recovery` | -ENOMSG/-EINVAL/-EIO 로그·상태 보존·재시도 없이 다음 요청 복귀 |
| `test_status_encode_failure_does_not_submit` | 오류 주입 시 TX 제출 차단·다음 요청 복귀 |
| `test_incoming_status_report_is_ignored` | 응답 TYPE 수신 시 추가 응답 없음·상태 보존·default fallthrough 없음 |

이 테스트는 프레임 파싱 이후의 처리 함수 경계에서 실행합니다.
CRC·길이 오류 차단과 실제 통계 집계는 UART 테스트에서 별도로 검증합니다.
스레드 스케줄링, atomic 동시성, 실제 UART 응답 송신은 검증하지 않습니다.


## 2026-11-08 공통 프레임·CRC 분리 검증

UART·STATUS fixture를 `protocol_frame` 멤버 구조에 맞추고, 각 host 빌드에
`protocol_frame.c`와 `crc16.c`를 연결했습니다. Python CRC 상호운용 테스트도
파서 구현을 직접 include하지 않고 공개 헤더와 실제 C 모듈을 링크합니다.
UART 20개·STATUS 5개·CRC 상호운용 18개가 통과했습니다.
펌웨어 빌드는 통과했으며 미사용 TX `crc` 변수와 기존 devicetree 경고가 남습니다.
TX 인코딩·실제 송신과 실보드 검증은 이번 검증 범위에 포함하지 않습니다.


## TX와 인코더 회귀 테스트

| 테스트 | 검증 내용 |
|---|---|
| `test_tx_init` | 장치 준비 실패 시 ready 미해제, 성공 시 해제 |
| `test_tx_invalid_submit` | NULL·길이 0/33/255 요청이 큐에 들어가지 않음 |
| `test_tx_queue_copy_fifo_full_and_recovery` | 원본 수정 후 복사본 유지, 8개 FIFO 순서, 포화 반환, 배출 후 새 요청 송신 |
| `test_tx_encode_error_continues` | 잘못된 큐 항목 인코딩 실패 시 송신하지 않고 다음 정상 프레임 처리 |
| `test_firmware_encoder_matches_python_and_parser` (3개) | 최소·최대 payload와 특수 바이트의 Python 바이트열 일치·파서 수용·출력 뒤 sentinel 보존 |
| `test_encoder_rejects_invalid_length_without_writing` (3개) | 길이 0/33/255 거부, 출력 버퍼 불변 |
| `test_encoder_rejects_null_arguments` | NULL 프레임·출력 포인터 거부 |
| `test_encoder_capacity_boundary` | 용량 0·한 바이트 부족 거부, 정확한 최대 크기 허용 |

TX host는 실제 `uart_tx.c` 워커를 동기 실행하고 UART 출력 바이트를 수집합니다.
빈 큐에서 실제 대기할 지점에 테스트를 종료하며 RTOS 스케줄링·동시성·물리 송신은 검증하지 않습니다.
STATUS에서 TX로 제출하는 호출부는 아직 연결되지 않았습니다.

## 2026-11-10 폴더 분리 검증

`fcc_app/src`를 transport/protocol/application/tracking/common으로 분리했습니다.
함수 본문·타입·프로토콜 값은 유지하고 include, CMake 소스, Python C 래퍼 경로만 조정했습니다.

- 이동 전후 동일 검증 통과: RX 20, STATUS 5, TX 4, Tracking CTest 1 suite(내부 13 사례).
- Python 전체: `82 passed, 9 subtests passed` (이동 전후).
- STM32F746G-DISCO 펌웨어 빌드 성공. 재구성 시 Device Tree의
  `memory-placeholder@90000000`/`memory@90000000` 주소 중복 경고가 출력됨.
- firmware와 네 host suite의 compile database를 갱신하고 소스 파일 존재·src include root 확인.
- 로컬 VS Code 설정을 갱신하고 `tools/vscode/`에 같은 설정의 예제를 저장.
  JSON·경로 검증을 수행했으며 VS Code 화면의 진단 표시는 별도 확인하지 않음.
- flash·실보드 UART/GUI HIL은 이번 폴더 분리 작업에서 수행하지 않음.


## 2026-11-10 enum 및 요청·보고 TYPE 정리

- enum 접두사 통일 후 MEAS_REPORT(0x02), STATUS_REQUEST(0x03)로 의미를 명시.
- STATUS_REPORT(0x06)는 예약 정의만 추가. 응답 payload·송신·GUI 수신 구현은 별도 작업.
- `test_frame_type_values_match_firmware`: C/Python의 여섯 TYPE 상수 값 일치 검증.
- `test_reserved_status_report_framing`: 임시 payload로 예약 TYPE의 framing/CRC 파싱만 검증.
  실제 STATUS 응답 내용이나 처리 성공을 검증하는 테스트는 아님.
- 최종 Python 84개 + subtest 9개, RX 20개, STATUS 5개, TX 4개,
  Tracking CTest 1 suite 통과. STM32 펌웨어 빌드 통과. flash/실보드 시험 미실시.


## 2026-11-10 assert 제외 코딩 규칙 적용

- 출력 포인터·인자명·단위·const·서식·헤더 의존성·공통 상수를 정리.
- RX `test_rx_init_errors`: 미준비 -ENODEV, callback -ENOSYS/-ENOTSUP 전달,
  실패 시 RX IRQ/준비 신호 억제, 성공 후 활성화 검증. RX 총 21개.
- TX init 테스트는 미준비 시 정확한 -ENODEV를 확인하도록 보강.
- STATUS 5개, TX 4개, Tracking 1 suite, Python 84개 + subtest 9개 검증.
- 펌웨어 빌드와 공개 헤더 단독 컴파일 통과. 펌웨어 assert·빌드 설정은 추가/변경하지 않음. flash·실보드 시험 미실시.


## 2026-11-12 STATUS_REPORT 검토·테스트

STATUS host 10개와 펌웨어 빌드 통과. 펌웨어 수정 없이 테스트 CMake에 실제 STATUS
인코더·프레임 파서를 연결하고 TX 제출 모사·인코딩 실패 주입을 추가했습니다.
실보드·PC 응답 수신은 검증하지 않았으며 이전 날짜의 미구현 기록은 당시 상태입니다.


### PC STATUS payload decoder (2026-11-12)

- `test_decode_status_payload`: fixed bytes decode to 1, 23, 456, UINT32_MAX.
- `test_decode_status_payload_zero`: all four counters are zero.
- `test_decode_status_payload_invalid_length`: lengths 0, 15, 17 raise ValueError with a diagnostic message.
- New cases: 5 passed. Full Python suite: 89 passed, 9 subtests passed.
- Payload decoding only; serial reception, frame assembly, TYPE/CRC validation and board round-trip remain pending.


## Python: PC 수신 프레임 파서

소스: [test_frame_parser.py](../tests/test_frame_parser.py).
요구사항: FUT-001의 PC 수신 조립 부분, SPEC-WIRE의 프레임·CRC 규격.

| 테스트 함수 | 사례 수 | 검증 내용 |
| --- | --- | --- |
| `test_complete_frame` | 1 | 고정 INIT bytes → 불변 payload의 프레임, STX 대기 복귀, 빈 입력 |
| `test_byte_by_byte` | 1 | CRC 하위 바이트 전까지 반환 없음, 마지막 바이트에서 완성 |
| `test_split_frame` | 5 | INIT의 모든 분할 위치, 중간 빈 입력에도 조립 상태 보존 |
| `test_consecutive_frames` | 1 | 고정 INIT·STATUS를 한 번에 처리, 순서·반환 데이터 수명 보존 |
| `test_noise_before_frame` | 1 | STX 없는 선행 잡음 무시 |
| `test_stx_inside_payload` | 1 | payload의 STX를 일반 데이터로 처리 |
| `test_bad_crc_then_valid_frame` | 1 | CRC 오류 폐기·1회 집계·정상 복귀 |
| `test_bad_length_then_valid_frame` | 3 | LEN 0·33·255 거부·집계·정상 복귀 |
| `test_maximum_payload` | 1 | 32바이트 payload 보존 |
| `test_reset_discards_partial_frame_and_preserves_counters` | 1 | 부분 프레임 폐기, 통계 보존, 이전 CRC 꼬리 무시, 정상 복귀 |

2026-11-12: 파서 16개 통과, 전체 Python 105개 및 subtest 9개 통과.
이 테스트는 실제 serial 읽기·수신 시간 제한·STATUS TYPE별 유효성·CLI/GUI·보드 왕복을 검증하지 않는다.


## Python: STATUS 요청·응답 CLI

소스: [test_status_client.py](../tests/test_status_client.py). FUT-001의 CLI 수신에 연결한다.

| 함수 | 사례 수 | 검증 |
| --- | --- | --- |
| `test_status_response` | 2 | 정상·한 바이트 분할 응답, 요청 1회, timeout 설정 복원 |
| `test_skips_errors_and_other_types` | 1 | CRC/LEN 오류·다른 TYPE·잘못된 STATUS payload 뒤 정상 복귀 |
| `test_total_deadline_is_not_extended` | 3 | 무응답·부분 응답·지속 잡음에서도 전체 deadline 유지 |
| `test_late_response_is_rejected` | 1 | deadline 경계 응답 거부 |
| `test_io_failures_restore_timeouts` | 3 | 짧은 쓰기·쓰기 실패·읽기 실패 전달과 설정 복원 |
| `test_invalid_timeout_does_not_touch_port` | 4 | 0·음수·NaN·무한대 거부 |
| `test_cli_output_and_port_cleanup` | 2 | 성공/timeout 출력·종료 코드·포트 정리 |
| `test_cli_open_failure` | 1 | 포트 열기 실패 진단 |
| `test_cli_invalid_arguments` | 4 | 잘못된 CLI 인자는 포트를 열기 전에 거부 |

2026-11-12: CLI 21개, 전체 Python 126개와 subtest 9개 통과. 모사 시각으로 실행하며 실제 보드·OS serial timeout 정밀도를 검증하지 않는다.


## GUI STATUS 조회 회귀

[tests/test_gui.py](../tests/test_gui.py)에 다음 네 사례를 추가했습니다.

- `test_gui_status_success_timeout_and_retry_during_measurements`: 분할 응답·값/시각 표시, 중복 클릭 방지, timeout 후 이전 값 유지·재조회, 측정 송신 지속.
- `test_gui_disconnect_cancels_status`: 조회 중 연결 해제, 대기 상태 해제·버튼 비활성화·포트 정리.
- `test_worker_status_skips_invalid_frames_and_rejects_duplicates`: CRC/LEN 오류·다른 TYPE·잘못된 STATUS 뒤 정상 복귀, worker 중복 요청 방지.
- `test_preview_rejects_status_request`: 미리보기 요청 거부.

Qt offscreen과 가짜 serial로 실행합니다. 실보드 응답·IRQ 동시성 검증은 아닙니다.


## Handler 명령·측정 경유 timeout 회귀

| 함수 | 검증 |
| --- | --- |
| `test_init_command_resets_tracking` | INIT으로 트랙·감시 초기화, 새 측정 재감시, 모사 통계 STATUS 보존 |
| `test_reset_command_resets_tracking` | RESET으로 트랙·감시 초기화, 새 측정 재감시, 모사 통계 STATUS 보존 |
| `test_valid_measurements_arm_refresh_and_expire` | 첫 유효 측정 감시 시작·갱신·1000ms 경계·1회 초기화 |
| `test_invalid_measurements_do_not_arm_timeout` | 길이 오류·거리 0이 감시를 시작하지 않음 |
| `test_invalid_measurements_do_not_refresh_timeout` | 잘못된 길이·거리 0이 기존 트랙·수신 시각을 변경하지 않음 |
| `test_gate_rejection_refreshes_receive_timeout` | CONFIRMED에서 gate 거부는 COASTING으로 전이하고 수신 시각 갱신 |
| `test_timestamp_rejection_refreshes_receive_timeout` | 중복·역행 timestamp는 트랙을 유지하지만 수신 시각 갱신 |

handler 17개 모두 통과. INIT/RESET 통계는 모사 snapshot 응답 보존 검증이며 실제 RX 카운터 통합 검증은 아니다. 펌웨어 수정·flash·실보드 시험은 하지 않았다.


## 최신 STATUS 실보드 결과

`2aa57d9`에서 CLI·GUI STATUS 왕복, CRC/LEN/부분 프레임 timeout 통계 증가,
INIT/RESET 통계 보존, 추적 중 조회와 무수신 timeout을 확인했습니다.
[2026-11-12 시험 결과·원시 로그](hardware-test-2026-11-12-status.md)를 참고합니다.
이 기록은 이전 STATUS 실보드 미검증/연기 상태를 대체합니다. 강제 링 overflow·UART overrun·부하 시험은 남아 있습니다.


## 2026-11-14 overflow Kconfig host 설정

RX drain 21개·discard 18개 모두 통과. 기존 공통 16개는 양쪽 정책으로 컴파일해 실행합니다.
신규 discard 사례 `test_discard_drops_complete_frame_and_recovers`,
`test_discard_drops_completable_partial_frame`는 정상/부분 프레임 폐기·통계 보존·수신 복귀를 확인합니다.
사용자가 작성한 펌웨어 Kconfig는 수정하지 않고 테스트 설정과 문서를 변경했습니다.
STM32F746G-DISCO 펌웨어도 DRAIN(`build`)·DISCARD(`build_discard`) 각각 빌드에 성공했고,
각 빌드의 `.config`에서 선택한 정책만 활성화된 것을 확인했습니다.
flash·실보드 강제 overflow는 수행하지 않았습니다.

후속 실보드 검증: 두 정책을 각각 플래시하고 UART 회귀 16개 조건씩 통과했습니다.
기본 DRAIN으로 복원 후 STATUS 응답까지 확인했습니다.
[2026-11-14 결과와 로그](hardware-test-2026-11-14-kconfig.md)를 참고합니다.
강제 링 overflow의 정책별 동작은 실보드 미검증 상태입니다.


## 내부 assert host 검증

`tests/test_firmware_assertions.py`의 3개 사례는 NULL 링버퍼, 잘못된 parser 상태,
무한대 process noise를 별도 프로세스에서 주입합니다. SIGABRT와 오류 문구를 확인하며
NDEBUG 정의 상태에서도 host 어댑터가 검사를 유지하는지 검증합니다.
공통 헤더는 `fcc_app/tests/common/include/zephyr/sys/__assert.h`이고 host target에만 연결합니다.
펌웨어에서는 실제 Zephyr assert를 사용합니다. 이 시험은 보드 fatal 처리의 검증이 아닙니다.


2026-11-14 fmodf 변경: tracking 17개·handler 17개, Python 136개 + subtest 9개 통과.
일반·디버그 펌웨어 빌드 성공. 이번 변경의 플래시·실보드 시험은 수행하지 않았습니다.
각도 경계·다중 회전·큰 유한값·NaN/Inf assert 및 비정상 s의 무변경 반환을 보강했습니다.

후속 플래시·실보드 시험 완료: 일반·디버그 각각 UART 16개·각도 경계 6개 조건 통과.
[결과와 원시 로그](hardware-test-2026-11-14-fmodf.md). 이전 문단의 미실시 상태를 대체합니다.

## 2026-11-14 게이트 설정 회귀

요구사항 REQ-TRK-002 / SPEC-TRACK. 실제 `gating.c`를 설정별로 따로 컴파일합니다.
설정 조합과 네 가지 사례의 상세 내용은 [Tracking 테스트 안내](../fcc_app/tests/test_kalman/README.md#게이트-설정-행렬)를 참고합니다.
최소·좁은 값·기본값·넓은 값·최대값에서 부호 양쪽 경계 포함/초과, 두 게이트 AND,
±π 경계 통과/거부 및 180° 특례를 검증합니다. 모든 가능한 정수 조합을 전수 검사한 것은 아닙니다.
C host 97회, Python 136개 + subtest 9개 통과. 이번 설정 변경은 flash/보드 검증하지 않았습니다.

펌웨어 기본값(100 m/10°) 및 별도 override(250 m/30°) 빌드 성공과 생성된 `.config`를 확인했다.
기존 보드 devicetree 중복 unit-address 경고는 유지된다. 새 빌드의 Zephyr 버전 메타데이터
조회에서는 시스템 git의 Xcode 라이선스 경고가 있었지만 컴파일/링크는 성공했다.

## 게이트 설정 실보드 검증 (2026-11-14)

`610e01e`: 기본값 100 m/10°와 변경값 250 m/30° 각각 UART 16개·게이트 14개 통과.
REQ-TRK-002 / SPEC-TRACK의 거리 경계·각도 안팎·AND·wrap 및 설정에 따른 수용/거부 차이를 확인했다.
보드는 기본값/DRAIN으로 복원했다. 앞선 설정 변경의 실보드 미검증 기록은 이 결과로 보완한다.
[결과·원시 로그·검증 한계](hardware-test-2026-11-14-gating.md).

## RX/TX 데이터 선 물리 단절·복원

실제 선을 분리·복원한 5단계 시험에서 14개 판정 조건을 통과했다.
측정 무수신 timeout, STATUS 요청/응답 경로 단절, 재부팅 없는 정상 왕복·추적 복귀를 확인했다.
수동 분리 시각에 대한 정밀 지연, USB 제거·전원 재인가·오배선·부하 시험은 포함하지 않았다.
[결과와 원시 로그](hardware-test-2026-11-14-link-loss.md).
