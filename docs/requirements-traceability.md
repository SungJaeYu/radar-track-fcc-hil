# 요구사항·상세 규격·테스트 추적

기준일: 2026-11-18 (`da6ffe5` 이후 작업본). 현재 저장소 작업본의 소스·테스트와 기존 보드 기록을 검토한 개발 기준 초안이다.
이미 구현된 동작을 요구사항으로 명문화했으며, 이 문서 작성만으로 모든 설계 선택이 최종 확정되거나
미검증 동작이 검증된 것으로 바뀌지 않는다. 프로토콜·정책 변경 시 아래 절차로 함께 갱신한다.

## 현재 기준 요약

기능 기준: TRACK_REPORT·radar_processor/report_sender 분리 `87006b0`, 이름·경로 정리 `fd96c17`, 로그 시험기 판정 `838aca6`.
현재 등록 사례는 Python 231개 + subtest 9개, C host 116회 실행(Tracking 17·게이트 설정 20·handler 30·RX drain 24/discard 21·TX 4)이다.
오래된 측정 폐기, STATUS 32바이트, CSV/평가를 추가했다. 기본 실보드 TRACK 왕복과 정상·잡음·연속 누락 평가, 실제 UART GUI를 확인했다. [실행 이력](verification-history.md).
PC 프레임 파서·CLI·GUI STATUS 수동 조회는 구현·host 검증 완료. 실보드 기본 왕복도 `2aa57d9`에서 확인했다.
아래 단계별 구현 기록은 당시 상태이며 현재 판단은 이 요약과 요구사항 기준표를 우선한다.

## v1 완료 범위 — 2026-11-15 계획 조정

v1의 완료 판정은 [README V1-1~5](../README.md#v1-acceptance)를 따릅니다. 단일표적 결과 반환·RMSE·시간/자원·오류 복귀·재현 자료가 필수이며, 다중표적·BIT·COMMAND/Lock On과 모든 하드웨어 오류 시험의 완성은 후속 범위입니다. 이 절은 release 범위 결정이며 아래 요구사항 ID·구현 상태·과거 시험 결과를 완료로 바꾸지 않습니다.

TRACK_REPORT 측정 경로는 아래 REQ-RPT-001~003 및 SPEC-TRACK-REPORT에 연결합니다. PC 수신·GUI 표시는 REQ-PC-003 / REQ-GUI-002로 연결했습니다. 원시 로그·정량 평가·이벤트 보고와 기본 실보드 왕복을 확인했습니다. TX 포화·장시간 부하는 FUT-003/008의 남은 범위입니다.

## 읽는 방법과 범위

`요구사항 REQ → 상세 규격 SPEC → 구현 파일/함수 → 테스트 함수 → 실행 증거` 순서로 추적한다.
테스트에서 요구사항을 찾을 때는 뒤의 역추적 표를 사용한다. ID는 삭제 후 재사용하지 않는다.

- 요구사항: 시스템이 제공해야 할 관측 가능한 동작과 제약.
- 상세 규격: 바이트 배치, 임계값, 상태 전이, 실패 처리 등 합격 여부를 판단할 기준.
- 테스트 연결: 그 요구사항을 검증하는 사례의 위치. 연결되어 있다는 사실만으로 전체 검증 완료를 뜻하지 않는다.
- 검증 공백: 모사 환경으로 증명할 수 없는 사항과 아직 없는 사례.

입력은 추적 전 탐지 측정이며 PC 정답 target ID를 전달하지 않는다. 현재 제품 범위는
단일표적 추적·진단 응답이며 RF/ADC/IQ/FFT는 범위 밖이다.
역할과 설계 근거는 [시스템 역할](system-roles.md), 실행 명령과 전체 사례 설명은
[테스트 목록](test-cases.md), 배선은 [보드 검증](board-bringup.md)을 따른다.
이 문서는 학습 프로젝트의 개발·검증 추적표이며 인증 적합성이나 운용 성능을 증명하지 않는다.

## 요구사항 기준표

`구현`은 해당 경로가 소스에 있다는 뜻이다. `부분 검증`은 연결된 테스트가 일부 조건만 확인한다는 뜻이다.
아래 C 테스트 연결은 모두 host 환경이다. 실보드 근거는 별도 증거 절에서만 판단한다.
상세 테스트 함수 이름과 역방향 연결은 테스트 역추적 표에 기록한다.

| ID | 요구사항 / 수용 조건 | 상세 규격 | 구현 근거 | 테스트 연결 | 검증 상태·남은 사항 |
| --- | --- | --- | --- | --- | --- |
| REQ-SYS-001 | PC는 시간·거리·방위각의 탐지 측정을 보내고 정답 target ID는 보내지 않아야 한다. | [SPEC-MEAS](#spec-meas) | [measurement_message.c](../fcc_app/src/protocol/measurement_message.c) · `measurement_message_decode` | [test_tracking](#tests-test_tracking) / [Python](#tests-python) | 구현; payload·시나리오 제한 시험. 다중표적 연관은 별도 범위 |
| REQ-IF-001 | 데이터 UART와 진단 콘솔을 분리하고 정해진 핀·baud로 연결해야 한다. | [SPEC-EXEC](#spec-exec) | [uart_rx.c](../fcc_app/src/transport/uart_rx.c) · `uart_rx_init` | [보드 기록](hardware-test-2026-11-01.md) · 소스 검토 | 구현; RX/TX 기본 왕복·물리 선 단절/복귀 확인. 부하 시험은 별도 |
| REQ-PRO-001 | LEN 1..32와 STX/TYPE/payload/CRC 배치를 지키고 완성된 유효 프레임만 제출해야 한다. | [SPEC-WIRE](#spec-wire) | [frame_parser.c](../fcc_app/src/protocol/frame_parser.c) · `frame_parser_feed` | [test_uart_rx](#tests-test_uart_rx) / [test_frame_handler](#tests-test_frame_handler) / [Python](#tests-python) | 구현; host·Python/C 상호운용 연결 |
| REQ-PRO-002 | CRC-16/IBM-3740으로 오류를 검출하고 CRC·길이 오류 프레임을 처리 큐에 제출하지 않아야 한다. | [SPEC-WIRE](#spec-wire) | [protocol_frame.c](../fcc_app/src/protocol/protocol_frame.c) · `protocol_frame_crc` | [test_uart_rx](#tests-test_uart_rx) / [test_frame_handler](#tests-test_frame_handler) / [Python](#tests-python) | 구현; 변조·길이·정상 프레임 복귀 시험 |
| REQ-PRO-003 | 인코더는 정확한 바이트 수를 반환하고 잘못된 인자·용량 부족 시 출력 버퍼를 유지해야 한다. | [SPEC-WIRE](#spec-wire) | [protocol_frame.c](../fcc_app/src/protocol/protocol_frame.c) · `protocol_frame_encode` | [Python](#tests-python) | 구현; NULL·길이·용량 경계 시험 |
| REQ-RX-001 | 링버퍼는 FIFO 순서·순환을 유지하고 full 상태의 push가 기존 데이터를 훼손하지 않아야 한다. | [SPEC-RX](#spec-rx) | [ring_buffer.c](../fcc_app/src/common/ring_buffer.c) · `ring_push / ring_pop` | [test_uart_rx](#tests-test_uart_rx) | 구현; host 검증, 실제 IRQ 경쟁은 제외 |
| REQ-RX-002 | overflow 시 새 바이트를 버리고 선택한 복구 정책 실행 후 정상 수신을 재개해야 한다. | [SPEC-RX](#spec-rx) | [uart_rx.c](../fcc_app/src/transport/uart_rx.c) · `rx_recover_overflow` | [test_uart_rx](#tests-test_uart_rx) | 구현; drain 24·discard 21 host 통과. 기본 DRAIN 강제 overflow·복귀 확인; DISCARD/프레임 경계 행렬 제외 |
| REQ-RX-003 | 미완성 프레임의 바이트 처리가 1000ms 중단되면 조립 상태를 지우고 timeout을 한 번 집계해야 한다. | [SPEC-TIME](#spec-time) | [rx_frame_processor.c](../fcc_app/src/transport/rx_frame_processor.c) · `rx_frame_processor_handle_timeout` | [test_uart_rx](#tests-test_uart_rx) | 구현; 모든 중단 위치·경계·복귀 host 검증. 보드 부분 중단·집계·정상 복귀 확인; 전 경계 보드 검증은 별도 |
| REQ-RX-004 | 완성 프레임과 완료 시각을 복사 제출하고 큐 포화 시 새 프레임 드롭을 집계해야 한다. | [SPEC-EXEC](#spec-exec) | [rx_frame_processor.c](../fcc_app/src/transport/rx_frame_processor.c) · `submit_parsed_frame` | [test_uart_rx](#tests-test_uart_rx) | 구현; 제출 모사 검증. 실제 handler 큐 포화·동시성 추가 필요 |
| REQ-RX-005 | 초기화 실패는 errno를 전달하고 RX 활성화·준비 신호를 내지 않아야 한다. | [SPEC-EXEC](#spec-exec) | [uart_rx.c](../fcc_app/src/transport/uart_rx.c) · `uart_rx_init` | [test_uart_rx](#tests-test_uart_rx) | 구현; 장치 미준비·callback 실패·성공 host 검증 |
| REQ-MEA-001 | 10바이트 측정을 해석하고 거리 0을 거부하며 방위각을 rad로 변환해야 한다. | [SPEC-MEAS](#spec-meas) | [measurement_message.c](../fcc_app/src/protocol/measurement_message.c) · `measurement_message_decode / measurement_message_validate` | [test_tracking](#tests-test_tracking) / [Python](#tests-python)  [test_frame_handler](#tests-test_frame_handler) | 구현; 변환·길이·거리 검증. handler의 잘못된 길이·거리 0에 대한 감시 영향 host 검증 완료 |
| REQ-TRK-001 | 거리와 방위각을 CV 칼만 필터로 예측·보정하고 각도 오차의 순환을 처리해야 한다. | [SPEC-TRACK](#spec-track) | [kalman_cv.c](../fcc_app/src/tracking/kalman_cv.c) · `kalman_cv_predict / kalman_cv_correct_angle` | [test_tracking](#tests-test_tracking) | 구현; 수치·공분산·수렴 host 검증. 실측 정확도 목표 미정 |
| REQ-TRK-002 | 거리·방위각 게이트를 모두 만족하는 측정만 보정에 사용해야 한다. | [SPEC-TRACK](#spec-track) | [gating.c](../fcc_app/src/tracking/gating.c) · `gating_check` | [test_tracking](#tests-test_tracking)  [test_frame_handler](#tests-test_frame_handler) | 구현; 기본/최소/좁은/넓은/최대 설정 20개 경계·AND·wrap 검증. handler의 gate 거부·COASTING·수신 시각 갱신 host 검증 완료 |
| REQ-TRK-003 | 중복·역행 시각을 거부하고 정상 wraparound를 허용하며 1000ms 초과 예측 간격은 트랙을 지워야 한다. | [SPEC-TRACK](#spec-track) | [track.c](../fcc_app/src/tracking/track.c) · `track_predict` | [test_tracking](#tests-test_tracking) | 구현; timestamp 경계·복귀 host 검증 |
| REQ-TRK-004 | hit/miss에 따라 트랙을 생성·확인·coasting·lost 처리하고 재시작해야 한다. | [SPEC-TRACK](#spec-track) | [track.c](../fcc_app/src/tracking/track.c) · `track_start / track_hit / track_miss` | [test_tracking](#tests-test_tracking) | 구현; 상태 전이 host 검증 |
| REQ-CTL-001 | INIT/RESET은 트랙과 측정 timeout 감시를 초기화하되 RX 누적 통계는 보존해야 한다. | [SPEC-TIME](#spec-time) | [frame_handler.c](../fcc_app/src/application/frame_handler.c) · `frame_handler_process / frame_handler_reset` | [Python](#tests-python)  [test_frame_handler](#tests-test_frame_handler) | 구현; 명령 경유 초기화·모사 통계 응답 보존 host 검증. 실보드 CRC/LEN/timeout 누적값 보존 확인; 큐·overflow 증가 후 통합 시험은 남음 |
| REQ-CTL-002 | 첫 유효 측정 이후 설정된 제한 시간(기본 1000ms) 동안 유효 측정이 없으면 트랙을 한 번 초기화해야 한다. | [SPEC-TIME](#spec-time) | [frame_handler.c](../fcc_app/src/application/frame_handler.c) · `frame_handler_handle_timeout` → [radar_processor.c](../fcc_app/src/application/radar_processor.c) · `radar_processor_is_timed_out` | [test_frame_handler](#tests-test_frame_handler) | 구현; 정상/무효 측정·gate/timestamp 거부·경계·1회 만료 host 검증 완료 |
| REQ-STA-001 | RX 오류 네 항목을 누적 집계하고 조회·파서 reset으로 초기화하지 않아야 한다. | [SPEC-STATUS](#spec-status) | [rx_frame_processor.c](../fcc_app/src/transport/rx_frame_processor.c) · `rx_frame_processor_get_stats` | [test_uart_rx](#tests-test_uart_rx) | 구현; 개별 atomic 조회 host 검증. 동시 snapshot 보장은 요구하지 않음 |
| REQ-STA-002 | STATUS 요청당 여덟 통계를 담은 32바이트 응답을 한 번 제출하고 트랙·측정 timeout은 변경하지 않아야 한다. | [SPEC-STATUS](#spec-status) | [report_sender.c](../fcc_app/src/application/report_sender.c) · `report_sender_submit_status` | [test_frame_handler](#tests-test_frame_handler) | 구현; 정상·반복 조회·인코딩·상태 보존 host 검증. 과거28바이트 CLI·GUI 및 현재32바이트 평가 CLI·GUI 왕복 확인 |
| REQ-STA-003 | STATUS 인코딩·제출 실패를 기록하고 즉시 재시도하지 않으며 이후 요청을 처리할 수 있어야 한다. | [SPEC-STATUS](#spec-status) | [report_sender.c](../fcc_app/src/application/report_sender.c) · `report_sender_submit_status` | [test_frame_handler](#tests-test_frame_handler) | 구현; 오류 주입·다음 요청 복귀 host 검증 |
| REQ-STA-004 | 입력된 STATUS_REPORT에는 응답을 생성하지 않아야 한다. | [SPEC-STATUS](#spec-status) | [frame_handler.c](../fcc_app/src/application/frame_handler.c) · `frame_handler_process` | [test_frame_handler](#tests-test_frame_handler) | 구현; 반향 응답 방지 host 검증 |
| REQ-STA-005 | 링 overflow 진입 횟수·ISR 폐기 바이트·복구 시 미소비 링 폐기 바이트를 누적 집계해야 한다. | [SPEC-RX](#spec-rx) / [SPEC-STATUS](#spec-status) | [uart_rx.c](../fcc_app/src/transport/uart_rx.c) · `uart_rx_get_stats` | [test_uart_rx](#tests-test_uart_rx) | 정책별 host 증가·누적·순환 검증, STATUS 전송 확인. DRAIN 실보드 overflow·ISR 폐기 증가 확인; DISCARD 제외 |
| REQ-TX-001 | 유효 요청을 큐에 복사하고 FIFO 순서로 송신하며 포화·잘못된 요청은 errno로 반환해야 한다. | [SPEC-TX](#spec-tx) | [uart_tx.c](../fcc_app/src/transport/uart_tx.c) · `uart_tx_submit / uart_tx_thread` | [test_uart_tx](#tests-test_uart_tx) | 구현; 모사 큐·바이트 순서·복사 검증. 실제 큐 동시성 미검증 |
| REQ-TX-002 | 장치 초기화 성공 후 송신을 시작하고 인코딩 실패 요청만 버린 뒤 다음 요청을 처리해야 한다. | [SPEC-TX](#spec-tx) | [uart_tx.c](../fcc_app/src/transport/uart_tx.c) · `uart_tx_init / uart_tx_thread` | [test_uart_tx](#tests-test_uart_tx) | 구현; host 검증. 기본 UART 전달 확인. 대기 경합·송신 완료 통지·부하 검증은 별도 |
| REQ-RPT-001 | TRACK_REPORT를 정의된 32바이트 배치·float32/BE·상태/결과 값으로 인코딩해야 한다. | [SPEC-TRACK-REPORT](#spec-track-report) | [track_message.c](../fcc_app/src/protocol/track_message.c) · `track_message_encode` | [test_frame_handler](#tests-test_frame_handler) | 구현; 고정 wire/CRC·float 비트·NULL/출력 보존 host 검증. C→PC 디코딩 및 기본 실보드 왕복 확인 |
| REQ-RPT-002 | 유효 측정 처리마다 결과를 보고하고 입력/추정 시각과 추정 유효성을 구분해야 한다. 무효 payload와 수신 보고 TYPE은 추가 보고를 만들지 않아야 한다. | [SPEC-TRACK-REPORT](#spec-track-report) | [radar_processor.c](../fcc_app/src/application/radar_processor.c) · `radar_processor_process_measurement`, [report_sender.c](../fcc_app/src/application/report_sender.c) · `report_sender_submit_track` | [test_frame_handler](#tests-test_frame_handler) | 구현; 시작·확정·거부·소실·재시작 host 검증. 제어/timeout 이벤트의 무효 보고도 host 검증;  PC stale 정책은 REQ-GUI-002 |
| REQ-RPT-003 | 보고 시도 순번을 순환 증가시키고 실패 시 소모하며 INIT/RESET·무수신 timeout에서 보존해야 한다. 실패 후 즉시 재시도 없이 다음 측정을 처리해야 한다. | [SPEC-TRACK-REPORT](#spec-track-report) | [report_sender.c](../fcc_app/src/application/report_sender.c) · `report_sender_submit_track`, [frame_handler.c](../fcc_app/src/application/frame_handler.c) · `frame_handler_reset` | [test_frame_handler](#tests-test_frame_handler) | 구현; 순환·초기화 보존·encode/submit 실패 후 복귀 host 검증. TX 실제 완료·유실 위치 식별은 미보장 |
| REQ-PC-001 | 송신 도구는 공통 wire 규격을 사용하고 연속 송신 시작마다 INIT을 보내야 한다. | [SPEC-PC](#spec-pc) | [protocol.py](../pc_sim/protocol.py) · `encode_frame` | [Python](#tests-python) | 구현; Python 송신·상호운용 시험 연결 |
| REQ-PC-002 | UART 시나리오는 단일표적으로 제한하고 지원하지 않는 다중 측정 설정을 거부해야 한다. | [SPEC-PC](#spec-pc) | [run_sim.py](../pc_sim/run_sim.py) · `UART scenario validation` | [Python](#tests-python) | 구현; Python 시나리오 시험 연결 |
| REQ-GUI-001 | GUI는 설정 접근·시작/중지/재시작·포트 정리를 제공하고 PC 합성값과 보드 결과를 구분해야 한다. | [SPEC-PC](#spec-pc) | [gui.py](../pc_sim/gui.py) · `GUI / worker lifecycle` | [Python](#tests-python) | 구현; Qt·모사 serial 시험, 이전 보드 GUI 기록. 보드 추정값 표시는 REQ-GUI-002로 추가 검증 |
| REQ-PC-003 | PC는 32바이트 TRACK payload의 필드·enum·flags·reserved·유한 수치·무효 필드 0을 검증하고 STATUS와 독립적으로 수신해야 한다. | [SPEC-TRACK-REPORT](#spec-track-report) / [SPEC-PC](#spec-pc) | [protocol.py](../pc_sim/protocol.py) · `decode_track_payload`, [gui_worker.py](../pc_sim/gui_worker.py) | [Python](#tests-python) | 구현; 고정 wire·C 인코더 상호운용·혼합/분할 UART 모사 및 기본 실보드 왕복 확인 |
| REQ-GUI-002 | PC 합성값과 보드 추정을 구분하고 추정 시각·유효 플래그로 표시하며, 2초 무수신을 PC STALE로 표시해야 한다. | [SPEC-PC](#spec-pc) | [gui_model.py](../pc_sim/gui_model.py) · `TrackMonitor`, [gui.py](../pc_sim/gui.py) | [Python](#tests-python) | 구현; 순번 wrap/중복/불연속·무효·STALE·모사 UART/Qt 검증. 실제 UART GUI·CSV·오프라인 정량 평가 확인 |
| REQ-MEA-002 | 큐에 오래 머문 측정은 처리·보고·무수신 감시를 갱신하지 않고 포화 집계해야 한다. | [SPEC-TIME](#spec-time) | frame_handler_process | test_stale_measurement_boundary_and_stats | host 경계·복귀·보존·포화 검증; 강제 보드 지연 미검증 |
| REQ-PC-004 | 실행별 입력/정답/응답/wire/STATUS를 보존하고 같은 추정 시각으로 RMSE를 계산해야 한다. | [SPEC-EVAL](#spec-eval) | recording.py / evaluate.py / gui_worker.py | test_recording.py / test_gui.py | host 및 고정 시나리오·실제 UART GUI 검증 |

## 상세 규격

<a id="spec-wire"></a>
### SPEC-WIRE — 프레임과 CRC

- wire: `7E | LEN | TYPE | PAYLOAD | CRC_H | CRC_L`; LEN은 payload만 센다.
- payload 길이 1..32, 전체 길이 6..37바이트. payload 내부 `0x7E`는 일반 데이터다.
- TYPE: INIT=1, MEAS_REPORT=2, STATUS_REQUEST=3, COMMAND=4, RESET=5, STATUS_REPORT=6, TRACK_REPORT=7.
  미지원 TYPE은 handler에서 로그 후 종료한다. COMMAND의 제어 동작은 아직 없다.
- CRC: poly `0x1021`, init `0xFFFF`, refin/refout=false, xorout=0.
  계산 대상은 LEN·TYPE·payload이고 STX·CRC 바이트는 제외한다. 상위 바이트부터 송신한다.
  표준 입력 `123456789`의 기대값은 `0x29B1`이다.
- 파서는 마지막 CRC 바이트까지 받아 검증한 경우만 FRAME_READY를 반환한다.
  CRC_ERROR/LENGTH_ERROR는 집계하고 제출하지 않으며, INCOMPLETE는 집계하지 않는다.
- 잘못된 LEN은 STX 대기로 돌아간다. 유효 범위 안에서 변조된 LEN은 다음 프레임 일부를
  소비할 수 있으므로 모든 손상에서 바로 다음 프레임을 반드시 복구한다는 보장은 하지 않는다.
- encode 성공은 생성 길이, 인자 오류는 `-EINVAL`, 출력 용량 부족은 `-ENOSPC`.
  오류 시 출력은 유지한다. TYPE은 1바이트 표현 가능하다는 호출 전제가 있다.

<a id="spec-meas"></a>
### SPEC-MEAS — 탐지 측정

| payload offset | 크기 | 표현 | 의미 |
| --- | --- | --- | --- |
| 0 | 4 | uint32 big-endian | 송신 측정 시각 ms |
| 4 | 2 | uint16 big-endian | 거리 m; 유효 측정은 1..65535 |
| 6 | 4 | int32 big-endian | signed semicircle; `rad = value × π / 2^31` |

payload는 정확히 10바이트이며 정답 ID를 포함하지 않는다. NULL·길이 오류는 decode에서 거부한다.
유효성 검사는 거리 0과 `[-π, π]` 밖의 각도를 거부한다. float 변환에는 반올림이 있으므로
정수 최대값이 float에서 π로 반올림되는 것을 wire 범위 초과로 해석하지 않는다.

<a id="spec-rx"></a>
### SPEC-RX — 버퍼와 overflow 복구

- 배열 256바이트, 한 칸을 비워 실제 저장 용량 255바이트. 인덱스는 uint8이다.
- full push는 실패하고 기존 바이트·head/tail을 유지한다.
- 첫 저장 실패 바이트를 버리고 overflow 플래그를 설정해 RX를 깨운다.
  복구가 끝날 때까지 ISR은 하드웨어 FIFO를 읽되 새 입력을 링에 넣지 않는다.
- 기본 정책 DRAIN_BUFFER: 기존 parser 상태와 이미 저장된 바이트로 완성할 수 있는
  프레임을 처리하고, 남은 불완전 프레임·링 상태·overflow 플래그를 정리한다.
- DISCARD_ALL: 기존 버퍼와 부분 프레임을 모두 폐기한다. 선택은 Kconfig choice와 prj.conf로 지정하며 `uart_rx.c`에서 enum 정책으로 매핑한다.
  host 테스트는 별도 compile definition으로 drain·discard를 각각 컴파일한다.
- 파싱·큐 제출·로그는 IRQ 잠금 밖에서 수행한다. host의 lock 표시는 실제 IRQ 경쟁 증명이 아니다.

<a id="spec-exec"></a>
### SPEC-EXEC — UART·스레드·큐 경계

- USART6 PC6 TX / PC7 RX, 115200 8N1, 외부 3.3V USB-UART; ST-Link는 콘솔·flash.
- ISR → 링 → RX 스레드 → 프레임 복사 큐 → handler 스레드 → TX 복사 큐 → TX 스레드.
- 현재 RX/handler/TX 우선순위는 5/6/7, 스택은 1024/2048/1024바이트다.
  이 값은 현재 구성이고 스택 여유·실시간 지연 충족의 증거가 아니다.
- parser는 RX, track·무수신 감시 상태는 handler 소유의 radar_processor, 보고 순번은 handler 소유의 report_sender, UART 송신은 TX가 담당한다. 새 application 모듈은 기존 handler 스레드에서 실행한다. 프레임 큐와 TX 큐는 각 8항목.
- RX/handler는 최대 50ms 대기를 사용해 무수신에도 검사 기회를 만든다.
  timeout 임계값과 실제 스케줄링 지연을 포함한 응답 시간은 별개다.
- `received_at_ms`는 프레임 완성·제출 시점의 보드 uptime이다. 물리적 첫/마지막 바이트 도착 시각이 아니다.
- submit은 객체를 복사하고 K_NO_WAIT를 사용한다. NULL/잘못된 LEN은 `-EINVAL`.
  RX 제출 `-ENOMSG`만 queue_drops로 집계한다. 다른 실패는 로그로 구분한다.
- RX 초기화 실패 시 IRQ enable·준비 신호를 내지 않는다. TX도 장치 준비 성공 후 준비 신호를 낸다.
- 큐 포화 시 INIT/RESET도 유실될 수 있다. 오래된 측정은 아래 SPEC-TIME에 따라 폐기한다. 제어 우선 보존·reset 이전 큐 폐기는 미정이다.

<a id="spec-time"></a>
### SPEC-TIME — 서로 다른 시간 조건

| 조건 | 기준 시계·시점 | 경계와 결과 |
| --- | --- | --- |
| 부분 프레임 timeout | RX가 마지막 바이트를 처리한 보드 uptime | 조립 중 경과 `>=CONFIG_RADAR_FRAME_RX_TIMEOUT_MS` (기본 1000ms)이면 parser reset·frame_timeouts 1 증가. STX 대기이면 감시하지 않음 |
| 측정 무수신 timeout | 마지막 decode·validate 성공 프레임의 received_at_ms | 첫 유효 측정 후에만 감시. 경과 `>=CONFIG_RADAR_MEASUREMENT_TIMEOUT_MS` (기본 1000ms)이면 트랙 reset·감시 해제·로그 1회 |
| 큐 측정 나이 | handler 처리 시 보드 uptime − received_at_ms | `>=CONFIG_RADAR_MEASUREMENT_TIMEOUT_MS`이면 MEAS_REPORT 폐기, stale_measurements 포화 증가; 추적·감시·보고 순번 불변 |
| 예측 간격 | 송신 측정 timestamp의 uint32 차이 | `>1000ms`이면 트랙 삭제. 정확히 1000ms는 예측 허용 |

부분 프레임은 바이트를 처리할 때마다 기준 시각을 갱신한다. 전체 프레임 조립 시간이
1000ms를 넘더라도 바이트 사이의 처리 간격이 제한 이내이면 보존한다.

큐 나이 검사를 통과하고 측정 decode·validate가 성공하면 이후 timestamp/게이팅에서 거부되어도 무수신 기준 시각은 갱신한다.
INIT/RESET은 트랙과 감시를 지우고 RX 통계는 지우지 않는다. STATUS는 감시 시작·갱신·해제를 하지 않는다.
무수신 검사와 실제 스레드 wakeup 지연을 포함한 최대 검출 지연은 아직 보드에서 규정·검증하지 않았다.

<a id="spec-track"></a>
### SPEC-TRACK — 단일표적 추적

- 거리와 방위각에 각각 상태 `[값, 변화율]`의 CV 필터를 사용한다.
  `dt = delta_ms / 1000`; 각도 보정은 wrap된 최소 각도 차이를 사용한다.
  Q/R 초기 튜닝값은 [track.c](../fcc_app/src/tracking/track.c)의 단위 주석을 기준으로 하며 센서 교정값으로 간주하지 않는다.
- 게이트는 `|거리 오차| <= CONFIG_RADAR_RANGE_GATE_M` 그리고 `|wrap 각도 오차| <= CONFIG_RADAR_AZIMUTH_GATE_DEG × π / 180`인 직사각형 게이트다. 기본값은 100 m·10°이고 경계를 포함한다.
- Kconfig 허용 범위는 1–65535 m·정수 1–180°다. 180°는 모든 정규화 각도 오차를 허용한다. 설정별 검증은 `gating_cases.c`의 `range_boundary`, `angle_boundary`, `both_required`, `wrapped_error`를 5개 조합에서 실행한다.
- EMPTY에서 측정으로 TENTATIVE 생성, hit_count=1. 총 3 hit에서 CONFIRMED.
  TENTATIVE miss는 EMPTY. CONFIRMED miss는 COASTING, 누적 5 miss에서 LOST.
  COASTING에서 hit는 CONFIRMED로 복귀하고 miss_count를 지운다.
- miss는 현재 radar_processor의 게이트 거부에 의해 발생한다. 무수신 시간마다 miss가 자동 증가하는 구조는 아니다.
- delta=0 또는 unsigned delta>=`0x80000000`은 거부한다. 정상 uint32 wraparound는 허용한다.
- LOST 또는 긴 간격은 predict에서 EMPTY로 만들고 TRACK_PREDICT_RESULT_RESET을 반환한다.
  radar_processor는 해당 측정을 재사용해 시작하지 않으며 다음 측정에서 새 트랙을 시작한다.
- RMSE 비교 방법과 초기 부하/지연 목표는 [v1 기준](../README.md#v1-acceptance)을 따른다. 최종 지원 부하와 실측 정확도·연속성은 아직 검증하지 않았다.

<a id="spec-track-report"></a>
### SPEC-TRACK-REPORT — 단일 트랙 측정 결과

현재 펌웨어의 TYPE=7, payload 32바이트·전체 wire 37바이트 규격이다.
`track_message_encode()`가 필드별로 직렬화하며 C 구조체 메모리를 그대로 전송하지 않는다.
PC payload 디코더·GUI 표시는 구현했고 C 인코더 상호운용 및 모사 UART로 검증했다. 로그 부하 시험기는 CRC/길이만 검사한다. 별도 evaluate CLI는 실제 보드 결과를 CSV에 기록하고 정량 평가한다.

| payload offset | 크기 | 표현 | 필드·의미 |
| --- | --- | --- | --- |
| 0 | 4 | uint32 BE | report_sequence — 보고 시도 순번 |
| 4 | 4 | uint32 BE | input_timestamp_ms — 이번 입력의 센서 시각(ms) |
| 8 | 4 | uint32 BE | estimate_timestamp_ms — 현재 추정 상태의 센서 기준 시각(ms) |
| 12 | 4 | IEEE 754 binary32 BE | range_m — 추정 거리(m) |
| 16 | 4 | IEEE 754 binary32 BE | azimuth_rad — 추정 방위각(rad) |
| 20 | 4 | IEEE 754 binary32 BE | range_rate_mps — 거리 변화율(m/s) |
| 24 | 4 | IEEE 754 binary32 BE | azimuth_rate_radps — 방위각 변화율(rad/s) |
| 28 | 1 | uint8 | state — EMPTY=0, TENTATIVE=1, CONFIRMED=2, COASTING=3, LOST=4 |
| 29 | 1 | uint8 | result — STARTED=0, UPDATED=1, GATE_REJECTED=2, TIME_REJECTED=3, RESET=4 |
| 30 | 1 | bit flags | bit0 INPUT_PRESENT, bit1 ESTIMATE_VALID; 나머지는 현재 0 |
| 31 | 1 | uint8 | reserved — 현재 sender는 0으로 채움 |

- `radar_processor_process_measurement()`가 decode·validate를 통과한 측정을 처리하면 handler는 보고를 한 번 시도한다.
  gate/time 거부도 포함한다. 큐 나이 초과 및 무효 payload는 보고하지 않고 순번도 소모하지 않는다.
- 첫 측정은 STARTED, 게이트 통과 갱신은 UPDATED, 게이트 거부는 GATE_REJECTED다.
  중복·역행 시각은 TIME_REJECTED이며 입력 시각은 거부된 값을 그대로 담고 추정 시각은 이전 값을 유지한다.
  predict가 LOST 또는 1000ms 초과 간격으로 트랙을 지우면 RESET이며 이 입력을 재사용해 시작하지 않는다.
  RESET은 INPUT_PRESENT=1이면 측정 처리 중 초기화, 0이면 INIT/RESET 또는 무수신 timeout 초기화이다. 입력 없는 이벤트의 원인별 구분은 하지 않는다.
- INPUT_PRESENT는 측정 보고에서 1, INIT/RESET·무수신 timeout 보고에서 0이다. 추정 유효 상태는 TENTATIVE/CONFIRMED/COASTING이다.
  EMPTY/LOST이면 ESTIMATE_VALID=0이고 추정 시각·거리·각도·두 변화율을 0으로 채운다.
  수신자는 숫자 0을 무효의 표시로 사용하지 않고 플래그를 확인해야 한다. COASTING의 값은 보정 없이 예측된 값이다.
- 순번은 부팅 초기화 후 0부터 시작하고 보고 시도마다 uint32로 순환한다. 인코딩 또는 TX 제출 실패도 순번을 소모한다.
  즉시 재시도하지 않고 이후 측정은 계속 처리한다. INIT/RESET·무수신 timeout은 순번을 보존한다.
  순번 누락만으로 어느 단계에서 유실됐는지 확정할 수 없으며 입력 단계에서 버린 측정은 순번을 소모하지 않는다.
- `track_message_encode()`는 성공 0, NULL 입력/출력은 `-EINVAL`이며 실패 시 출력을 보존한다.
  필드 의미 검증은 수행하지 않는다. 현재 sender가 유효성·reserved 정책을 적용한다.
- 큐 접수는 물리 송신이나 PC 수신 완료가 아니다. 큐가 차면 새 보고를 거부하며 우선순위·완료 통계는 미정이다.
- INIT/RESET·무수신 timeout은 초기화 후 RESET 보고를 한 번 시도한다. 입력/추정 시각·수치는 0, 두 유효 플래그도 0이다. 초기화 자체는 순번을 리셋하지 않으며 이벤트 보고 시도가 순번을 하나 소모한다. 실패 시 재시도하지 않는다. PC stale 판정은 이벤트 유실에 대비해 유지한다.
- 상태 숫자는 현재 `TrackState` 선언 순서에 의존한다. 외부 규격으로 사용하므로 변경 시 명시적 값 또는 매핑과 C/Python 회귀가 필요하다.

<a id="spec-status"></a>
### SPEC-STATUS — 오류 통계와 응답

STATUS_REQUEST는 공통 LEN 1..32만 요구하며 현재 payload 내용은 무시한다.
조회값은 STATUS_REPORT(TYPE=6)에 사용하며 DEBUG 로그는 큐 접수만 기록한다.

| 응답 offset | 크기 | 필드 |
| --- | --- | --- |
| 0 | 4 | crc_errors |
| 4 | 4 | length_errors |
| 8 | 4 | frame_timeouts — 부분 프레임 timeout |
| 12 | 4 | queue_drops — RX → handler 큐 포화 드롭 |
| 16 | 4 | ring_overflow_events — overflow 복구 진입 횟수 |
| 20 | 4 | rx_dropped_bytes — ISR에서 저장하지 못한 바이트 |
| 24 | 4 | recovery_discarded_bytes — 복구 시 링 미소비 바이트 폐기 |
| 28 | 4 | stale_measurements — handler가 큐 나이 초과로 폐기한 측정 수 |

각 필드는 big-endian uint32이고 payload 32바이트, 전체 프레임 37바이트다.
`STATUS_REPORT_PAYLOAD_LENGTH`는 `STATUS_REPORT_FIELD_COUNT × 4U`로 계산한다.
현재 all-uint32 구조체 크기 일치와 최대 payload 용량은 정적 assert로 검사한다.
이는 필드 개수 자동 추론이나 직렬화 검증을 대신하지 않는다. 필드 추가 시 개수·인코더·PC 규격·테스트를 함께 갱신한다.
PC는 구형 16/28바이트 응답도 해석하고 없는 필드는 미지원으로 표시한다. 새 펌웨어는 32바이트만 송신한다.
RX 일곱 값은 각각 atomic으로 읽고 stale_measurements는 handler 소유 상태에서 읽는다. 전체 동시 snapshot은 보장하지 않는다.
UART 하드웨어 오류·TX 실패·측정 무수신은 이 여덟 항목에 포함하지 않는다.
폐기 바이트는 하드웨어 유실과 parser가 소비한 부분 프레임을 포함하지 않는다.
stale_measurements는 UINT32_MAX에서 포화하며 INIT/RESET/timeout으로 지우지 않는다. 기존 RX 카운터의 최대값 정책과 실제 BIT 항목은 후속 설계 대상이다.

인코딩 실패 시 TX 제출하지 않고, 제출 실패 시 로그 후 종료한다. 즉시 재시도하지 않는다.
다음 STATUS 요청은 다시 처리한다. 외부에서 STATUS_REPORT가 들어오면 로그만 남기고 응답하지 않는다.

<a id="spec-tx"></a>
### SPEC-TX — 송신 계약

유효 요청은 8항목 큐에 복사한다. 포화 시 `-ENOMSG`이며 새 요청은 들어가지 않는다.
TX worker는 초기화 준비 신호 이후 FIFO 순서로 인코딩·polling 송신한다.
인코딩 실패는 해당 요청만 폐기하고 다음 요청을 진행한다.
submit 성공은 큐 접수 성공이며 송신 완료·PC 수신·ACK 성공을 뜻하지 않는다.
현재 UART polling API는 peer 수신 성공을 반환하지 않으며, PC 파서·STATUS CLI·GUI가 응답을 검증·해석한다. 실보드 기본 전달은 `2aa57d9`에서 확인했다.

<a id="spec-pc"></a>
### SPEC-PC — 시뮬레이터와 GUI

연속 송신은 INIT 후 측정을 시작하고 재시작 시 새 INIT과 timestamp 0부터 시작한다.
측정 wire 생성은 [protocol.py](../pc_sim/protocol.py)를 공유한다.
UART 시나리오는 단일표적·클러터 없음으로 제한하고 다중표적 생성은 PC 출력용으로 남긴다.
GUI는 설정 탭·스크롤 접근, 시작/중지/재시작·연결 종료·실패 시 포트 정리를 제공한다.
PC 합성 실제값·측정값과 TYPE=7의 보드 추정값을 구분한다. 미리보기는 보드 추정을 만들지 않는다.
하나의 수신 파서를 STATUS/TRACK이 공유하고 STATUS 요청/timeout 때문에 부분 TRACK 조립을 초기화하지 않는다.
TRACK payload는 32바이트, 알려진 enum/flag, reserved 0, 유한 float, 상태/유효성 일치와 무효 추정 필드 0을 요구한다. INPUT_PRESENT=0일 때 입력 timestamp는 0이어야 한다. 검증 실패는 해당 보고만 제외한다.
거리 그래프는 추정 시각이 보관한 PC 측정 timestamp와 일치할 때만 대응한다. 무효/대응 불가 보고는 곡선을 끊는다. 무효 추정은 현재 위치에서 숨기고 수치 대신 `—`로 표시한다.
PC monotonic 수신 시각부터 2초 이상 새 순번 보고가 없으면 STALE로 표시하고 현재 위치는 숨기며 이력은 흐리게 유지한다. 마지막 보드 state를 LOST로 바꾸지 않는다.
순번은 uint32 wrap을 허용하고 전진 간격의 누락 개수를 집계한다. 중복은 신선도를 갱신하지 않으며, 역행/반주기 이상 간격은 불연속으로 표시하고 이력만 새로 시작한다. 유실 위치나 재부팅 원인은 확정하지 않는다.
새 연결/시뮬레이션 시작은 PC 이력을 초기화한다. 세션 ID·INIT ACK가 없어 이전 실행의 대기 보고와 재사용 timestamp를 완전히 구분할 수는 없다. UART write 성공은 수신 ACK가 아니다.
모델과 GUI 상세 정책은 [GUI 안내](../pc_sim/GUI.md#track-수신과-유효성)를 따른다.

## 테스트 역추적

아래 이름은 실제 함수명이며 각 링크는 해당 테스트 소스로 이동한다.
한 사례가 여러 요구사항 일부를 확인할 수 있다. 구체적인 입력과 assertion은 링크한 소스가 기준이다.
기존 함수명 `test_status_report_framing`의 reserved는 과거 명칭이며 현재 미구현 상태를 뜻하지 않는다.

<a id="tests-test_uart_rx"></a>
### test_uart_rx

| 테스트 함수 | 연결 요구사항 |
| --- | --- |
| [`test_rx_init_errors`](../fcc_app/tests/test_uart_rx/src/main.c) | REQ-RX-005 |
| [`test_uart_stats_normal_receive`](../fcc_app/tests/test_uart_rx/src/main.c) | REQ-STA-005, REQ-RX-002 |
| [`test_uart_stats_overflow_and_recovery`](../fcc_app/tests/test_uart_rx/src/main.c) | REQ-STA-005, REQ-RX-002 |
| [`test_uart_stats_wrapped_ring_recovery`](../fcc_app/tests/test_uart_rx/src/main.c) | REQ-STA-005, REQ-RX-002 |
| [`test_stats_snapshot_and_reset`](../fcc_app/tests/test_uart_rx/src/main.c) | REQ-STA-001 |
| [`test_timeout_boundary_for_every_partial_frame`](../fcc_app/tests/test_uart_rx/src/main.c) | REQ-RX-003 |
| [`test_timeout_ignores_idle_and_complete_frames`](../fcc_app/tests/test_uart_rx/src/main.c) | REQ-RX-003 |
| [`test_timeout_recovers_next_valid_frame`](../fcc_app/tests/test_uart_rx/src/main.c) | REQ-RX-003 |
| [`test_timeout_preserves_split_frames`](../fcc_app/tests/test_uart_rx/src/main.c) | REQ-RX-003 |
| [`test_timeout_refreshes_on_each_byte`](../fcc_app/tests/test_uart_rx/src/main.c) | REQ-RX-003 |
| [`test_explicit_reset_cancels_partial_timeout`](../fcc_app/tests/test_uart_rx/src/main.c) | REQ-RX-003 |
| [`test_processor_copies_frame_and_reset_preserves_drops`](../fcc_app/tests/test_uart_rx/src/main.c) | REQ-RX-004, REQ-STA-001 |
| [`test_queue_full_counts_drop_and_recovers`](../fcc_app/tests/test_uart_rx/src/main.c) | REQ-RX-004, REQ-STA-001 |
| [`test_nonready_results_do_not_submit`](../fcc_app/tests/test_uart_rx/src/main.c) | REQ-PRO-001, REQ-PRO-002 |
| [`test_rejects_bad_crc`](../fcc_app/tests/test_uart_rx/src/main.c) | REQ-PRO-001, REQ-PRO-002 |
| [`test_drain_queue_full_still_finishes_recovery`](../fcc_app/tests/test_uart_rx/src/main.c) | REQ-RX-002, REQ-RX-004 |
| [`test_dispatch_drains_complete_frame`](../fcc_app/tests/test_uart_rx/src/main.c) | REQ-RX-002 |
| [`test_preserves_partial_parser_and_drops_new_input`](../fcc_app/tests/test_uart_rx/src/main.c) | REQ-RX-002 |
| [`test_discards_incomplete_frame`](../fcc_app/tests/test_uart_rx/src/main.c) | REQ-RX-002 |
| [`test_receives_after_recovery`](../fcc_app/tests/test_uart_rx/src/main.c) | REQ-RX-002 |
| [`test_empty_recovery`](../fcc_app/tests/test_uart_rx/src/main.c) | REQ-RX-002 |
| [`test_drain_rejects_bad_length_then_handles_valid_frame`](../fcc_app/tests/test_uart_rx/src/main.c) | REQ-RX-002, REQ-PRO-002 |
| [`test_ring_overflow_preserves_buffer`](../fcc_app/tests/test_uart_rx/src/main.c) | REQ-RX-001 |
| [`test_ring_wraparound`](../fcc_app/tests/test_uart_rx/src/main.c) | REQ-RX-001 |

<a id="tests-test_frame_handler"></a>
### test_frame_handler

| 테스트 함수 | 연결 요구사항 |
| --- | --- |
| [`test_stale_measurement_boundary_and_stats`](../fcc_app/tests/test_frame_handler/src/main.c) | REQ-MEA-002, REQ-STA-002, REQ-CTL-002 |
| [`test_status_zero`](../fcc_app/tests/test_frame_handler/src/main.c) | REQ-STA-002 |
| [`test_status_snapshot`](../fcc_app/tests/test_frame_handler/src/main.c) | REQ-STA-002 |
| [`test_status_preserves_track`](../fcc_app/tests/test_frame_handler/src/main.c) | REQ-STA-002 |
| [`test_status_encode_invalid_arguments`](../fcc_app/tests/test_frame_handler/src/main.c) | REQ-STA-002 |
| [`test_status_does_not_arm_timeout`](../fcc_app/tests/test_frame_handler/src/main.c) | REQ-STA-002, REQ-CTL-002 |
| [`test_status_does_not_refresh_timeout`](../fcc_app/tests/test_frame_handler/src/main.c) | REQ-STA-002, REQ-CTL-002 |
| [`test_status_encode_wire`](../fcc_app/tests/test_frame_handler/src/main.c) | REQ-STA-002, REQ-PRO-001, REQ-PRO-002 |
| [`test_status_submit_failure_and_recovery`](../fcc_app/tests/test_frame_handler/src/main.c) | REQ-STA-003 |
| [`test_status_encode_failure_does_not_submit`](../fcc_app/tests/test_frame_handler/src/main.c) | REQ-STA-003 |
| [`test_incoming_status_report_is_ignored`](../fcc_app/tests/test_frame_handler/src/main.c) | REQ-STA-004 |
| [`test_track_encode_wire`](../fcc_app/tests/test_frame_handler/src/main.c) | REQ-RPT-001, REQ-PRO-001, REQ-PRO-002 |
| [`test_track_encode_invalid_arguments`](../fcc_app/tests/test_frame_handler/src/main.c) | REQ-RPT-001 |
| [`test_track_started_updated_and_confirmed`](../fcc_app/tests/test_frame_handler/src/main.c) | REQ-RPT-002, REQ-TRK-004 |
| [`test_track_timestamp_rejection_report`](../fcc_app/tests/test_frame_handler/src/main.c) | REQ-RPT-002, REQ-TRK-003 |
| [`test_track_gate_rejection_empty_and_lost`](../fcc_app/tests/test_frame_handler/src/main.c) | REQ-RPT-002, REQ-TRK-002, REQ-TRK-004 |
| [`test_track_gap_reset_and_recovery`](../fcc_app/tests/test_frame_handler/src/main.c) | REQ-RPT-002, REQ-TRK-003 |
| [`test_track_sequence_wrap_and_timeout_preservation`](../fcc_app/tests/test_frame_handler/src/main.c) | REQ-RPT-003, REQ-CTL-002 |
| [`test_track_submit_failure_and_recovery`](../fcc_app/tests/test_frame_handler/src/main.c) | REQ-RPT-003 |
| [`test_track_encode_failure_and_recovery`](../fcc_app/tests/test_frame_handler/src/main.c) | REQ-RPT-003 |
| [`test_incoming_track_report_is_ignored`](../fcc_app/tests/test_frame_handler/src/main.c) | REQ-RPT-002 |
| [`test_processor_invalid_input_preserves_output`](../fcc_app/tests/test_frame_handler/src/main.c) | REQ-MEA-001, REQ-RPT-002 |
| [`test_processor_timeout_query_preserves_state`](../fcc_app/tests/test_frame_handler/src/main.c) | REQ-CTL-002, REQ-RPT-003 |
| [`test_init_command_resets_tracking`](../fcc_app/tests/test_frame_handler/src/main.c) | REQ-CTL-001, REQ-RPT-003 |
| [`test_reset_command_resets_tracking`](../fcc_app/tests/test_frame_handler/src/main.c) | REQ-CTL-001, REQ-RPT-003 |
| [`test_valid_measurements_arm_refresh_and_expire`](../fcc_app/tests/test_frame_handler/src/main.c) | REQ-CTL-002 |
| [`test_invalid_measurements_do_not_arm_timeout`](../fcc_app/tests/test_frame_handler/src/main.c) | REQ-CTL-002, REQ-MEA-001, REQ-RPT-002 |
| [`test_invalid_measurements_do_not_refresh_timeout`](../fcc_app/tests/test_frame_handler/src/main.c) | REQ-CTL-002, REQ-MEA-001, REQ-RPT-002 |
| [`test_gate_rejection_refreshes_receive_timeout`](../fcc_app/tests/test_frame_handler/src/main.c) | REQ-CTL-002, REQ-TRK-002 |
| [`test_timestamp_rejection_refreshes_receive_timeout`](../fcc_app/tests/test_frame_handler/src/main.c) | REQ-CTL-002, REQ-TRK-003 |

<a id="tests-test_uart_tx"></a>
### test_uart_tx

| 테스트 함수 | 연결 요구사항 |
| --- | --- |
| [`test_tx_init`](../fcc_app/tests/test_uart_tx/src/main.c) | REQ-TX-002 |
| [`test_tx_encode_error_continues`](../fcc_app/tests/test_uart_tx/src/main.c) | REQ-TX-002 |
| [`test_tx_invalid_submit`](../fcc_app/tests/test_uart_tx/src/main.c) | REQ-TX-001 |
| [`test_tx_queue_copy_fifo_full_and_recovery`](../fcc_app/tests/test_uart_tx/src/main.c) | REQ-TX-001 |

<a id="tests-test_tracking"></a>
### test_tracking

| 테스트 함수 | 연결 요구사항 |
| --- | --- |
| [`test_angle_wrap`](../fcc_app/tests/test_tracking/src/main.c) | REQ-TRK-001 |
| [`test_angle_wrap_boundaries`](../fcc_app/tests/test_tracking/src/main.c) | REQ-TRK-001 |
| [`test_angle_wrap_multiple_turns`](../fcc_app/tests/test_tracking/src/main.c) | REQ-TRK-001 |
| [`test_angle_wrap_large_finite_values`](../fcc_app/tests/test_tracking/src/main.c) | REQ-TRK-001 |
| [`test_correction_rejects_invalid_innovation_covariance`](../fcc_app/tests/test_tracking/src/main.c) | REQ-TRK-001 |
| [`test_cv_prediction_and_covariance`](../fcc_app/tests/test_tracking/src/main.c) | REQ-TRK-001 |
| [`test_cv_correction`](../fcc_app/tests/test_tracking/src/main.c) | REQ-TRK-001 |
| [`test_angle_prediction_and_correction`](../fcc_app/tests/test_tracking/src/main.c) | REQ-TRK-001 |
| [`test_stationary_convergence`](../fcc_app/tests/test_tracking/src/main.c) | REQ-TRK-001 |
| [`test_gate`](../fcc_app/tests/test_tracking/src/main.c) | REQ-TRK-002 |
| [`test_decode_semicircle`](../fcc_app/tests/test_tracking/src/main.c) | REQ-MEA-001, REQ-SYS-001 |
| [`test_timestamp_rejection`](../fcc_app/tests/test_tracking/src/main.c) | REQ-TRK-003 |
| [`test_elapsed_time_and_wraparound`](../fcc_app/tests/test_tracking/src/main.c) | REQ-TRK-003 |
| [`test_long_gap_and_restart`](../fcc_app/tests/test_tracking/src/main.c) | REQ-TRK-003 |
| [`test_confirmation_coasting_and_loss`](../fcc_app/tests/test_tracking/src/main.c) | REQ-TRK-004 |
| [`test_tentative_miss_and_inactive_calls`](../fcc_app/tests/test_tracking/src/main.c) | REQ-TRK-004 |
| [`test_lost_restarts_for_any_timestamp`](../fcc_app/tests/test_tracking/src/main.c) | REQ-TRK-004 |

<a id="tests-python"></a>
### Python 시험 묶음

Python의 개별 함수·매개변수 사례는 [테스트 목록](test-cases.md)에 유지한다.

| 테스트 소스 | 연결 요구사항 | 검증 경계 |
| --- | --- | --- |
| [test_crc_interop.py](../tests/test_crc_interop.py) | REQ-PRO-001, REQ-PRO-002, REQ-PRO-003, REQ-PC-003, REQ-RPT-001 | TRACK C 인코더→PC decode·state/result 값, CRC 기준값, C/Python 호환, 변조·길이 오류·복귀, CRC 마지막 바이트, encode 인자·용량, TYPE 값 |
| [test_recording.py](../tests/test_recording.py) | REQ-PC-004 | 시각 정렬·각도 wrap·중복/무효 제외·warm-up 재시작·덮어쓰기 방지·미리보기 배제·CLI 입력 검증 |
| [test_track_report.py](../tests/test_track_report.py) | REQ-PC-003 | 고정 payload·길이·의미 검증; UART/GUI 제외 |
| [test_protocol.py](../tests/test_protocol.py) | REQ-SYS-001, REQ-PRO-001, REQ-MEA-001, REQ-PC-001 | INIT/측정 wire, 길이 제한, semicircle·거리 입력 경계 |
| [test_frame_sender.py](../tests/test_frame_sender.py) | REQ-PC-001, REQ-CTL-001 | INIT/RESET 요청 생성·송신만 검증; handler의 reset 효과는 검증하지 않음 |
| [test_radar_sender.py](../tests/test_radar_sender.py) | REQ-PC-001, REQ-MEA-001 | 연속 송신·간격·단위·설정 오류 |
| [test_scenarios.py](../tests/test_scenarios.py) | REQ-SYS-001, REQ-PC-001, REQ-PC-002 | 단일 UART, 다중표적 제한, 미탐지, 포트 정리 |
| [test_gui_model.py](../tests/test_gui_model.py) | REQ-GUI-001, REQ-GUI-002, REQ-MEA-001 | 그래프 모델·wire·설정·거리 범위, 순번/STALE/추정 시각 |
| [test_log_stress.py](../tests/test_log_stress.py) | FUT-009, REQ-PRO-001, REQ-PRO-002 | 수집된 STATUS/TRACK framing·길이와 STATUS 개수/0 통계만 검증; TRACK 수치·실보드 부하는 제외 |
| [test_gui.py](../tests/test_gui.py) | REQ-GUI-001, REQ-GUI-002, REQ-PC-001, REQ-PC-003 | 화면 접근·worker·재시작·오류 정리, STATUS/TRACK 혼합·부분 조립·STALE 표시 |

## 실행 증거와 완료 판정

| 증거 | 대상·결과 | 해석 범위 |
| --- | --- | --- |
| 2026-11-12 테스트 형식 통일 후 host 실행 | RX 21, STATUS 10, TX 4, Tracking 13 통과. 전체/선택/잘못된 인자 실행 확인 | 현재 작업본 host 로직. 펌웨어 빌드·flash·실보드 검증을 뜻하지 않음 |
| [2026-11-12 STATUS 기록](test-cases.md) | STATUS 10 및 당시 펌웨어 빌드 통과 기록 | 당시 소스 기준; 이 문서 작성 중 새 빌드를 실행한 것은 아님 |
| [2026-11-01 보드 기록과 원시 로그](hardware-test-2026-11-01.md) | 정상 측정, INIT/RESET, 무수신, CRC/LEN 거부·복귀, 큐 burst, GUI | 과거 펌웨어 기준. 현재 작업본 전체의 보드 합격으로 승격하지 않음 |
| Python 시험 연결 | 기존 사례·기록에 연결 | 이 문서 작성 시 Python 재실행하지 않음. 최신 실행 결과는 별도로 기록할 것 |

요구사항의 검증 완료는 연결한 테스트의 실행 결과·대상 revision·환경·남은 공백을 함께 보고 판단한다.
모사 atomic/IRQ/큐는 실제 RTOS 동시성 증거가 아니다. 위 2026-11-01 보드 시험은 STATUS TX·부분 프레임
경계·실제 링 overflow를 검증하지 않았다. 이후 STATUS·timeout 실보드 결과는 [검증 이력](verification-history.md)을 따른다. 테스트 함수가 있다는 이유만으로 완료 표시하지 않는다.

재현 명령은 각 suite README를 따른다. 결과 기록에는 커밋 SHA(작업본이면 미커밋 변경 여부),
명령, 도구/보드 환경, PASS/FAIL, 로그 경로를 남긴다. 실패를 수정하면 이전 실패 기록과 재시험 결과를 구분한다.

## 확장 요구사항 진행 상태와 검증 공백

아래 FUT는 확장 요구사항이며 구현된 항목과 남은 항목을 구분한다. 아직 규격이 정해지지 않은 값은 TBD로 남기고 구현 완료로 표시하지 않는다.

| ID | 목표 / 완료 기준 | 선행 설계·검증 |
| --- | --- | --- |
| FUT-001 | PC가 STATUS 응답의 TYPE·길이·CRC를 검증하고 여덟 통계를 표시한다 | 파서·CLI·GUI 수동 조회 구현 및 host 검증 완료. 물리 왕복은 FUT-002 |
| FUT-002 | 현재 STATUS 응답을 실제 USART6에서 수신해 host 기대값과 일치함을 확인한다 | 완료: 과거28바이트 CLI·GUI 및 현재32바이트 평가 CLI·GUI 왕복. [확장 STATUS 실보드 결과](hardware-test-2026-11-14-status-extended.md) |
| FUT-003 | 보드 추적 결과를 PC로 보내 정답과 비교한다 | TYPE=7·32바이트 측정 보고와 host 검증 완료(REQ-RPT-001~003). PC 디코더/GUI·2초 STALE도 host 검증 완료. CSV·고정 시나리오 평가·실보드 왕복 완료. 이벤트 원인별 구분·TX 포화 관측 남음 |
| FUT-004 | 링 overflow·손실·UART 오류를 구분해 집계·복구한다 | 링 overflow·ISR/복구 폐기 집계와 STATUS 전송 구현. 하드웨어 오류 API·복구와 실제 overflow/overrun 재현은 남음 |
| FUT-005 | 제어 요청과 오래된 측정의 큐 처리 정책을 정의한다 | 오래된 측정 경계 폐기 구현·host 검증 완료. INIT/RESET 우선순위·reset 이전 대기 데이터 정책 TBD |
| FUT-006 | 다중 측정·트랙을 연관하고 펌웨어 자체 ID로 관리한다 | 스캔 규격·최대 표적 수·트랙 수명·교차/클러터 평가. 정답 ID 입력 금지 |
| FUT-007 | COMMAND/Lock On과 BIT를 제공한다 | 명령 payload·선택/해제·소실 동작·자기진단 항목과 합격 기준 TBD |
| FUT-009 | 로그를 모듈·레벨별로 관리하고 스레드 간 문장 섞임을 방지한다 | [다섯 모듈 deferred logging](logging.md)·일반 INFO/디버그 DEBUG·비차단 새 로그 폐기 구현. 일반/디버그 각 30조건 기본 실보드 회귀 완료. [강제 포화/유실·STATUS 유지 확인](hardware-test-2026-11-14-log-stress.md). 반복 WARN·최악 UART 영향·자원 측정은 남음 |
| FUT-008 | 설정·자원·지연을 반복 검증 가능한 기준으로 관리한다 | overflow·timeout·게이트 Kconfig와 host 검증 완료; 게이트 변경값 실보드 확인. 강제 overflow 실보드·스택·지연/처리량 실측 남음. 초기 50Hz·30분·보드 지연 예산 20ms는 [v1 목표](../README.md#v1-acceptance)이며 달성 증거는 아직 없음 |

FUT-001은 구현·host 검증 완료, FUT-002 기본 실보드 왕복도 완료했다. CTL-002 측정 경유 검증은 완료했다.
남은 host 공백은 CTL-001의 실제 RX 카운터와 명령 처리 통합, RX-004의 실제 큐 경계와 다른 errno 처리다.
이 목록은 현재 커버리지 공백이며 지금 모두 충족했다고 표시하지 않는다.

## 변경 시 추적 절차

1. 먼저 REQ/FUT의 관측 가능한 요구와 합격 기준을 수정한다. 의도가 바뀌면 변경 이유를 남긴다.
2. 해당 SPEC의 숫자·wire·시간 기준·오류 계약을 수정한다. 미정 값은 TBD로 남긴다.
3. 구현을 변경하고 요구사항별 정상·경계·실패·복귀 테스트를 추가/수정한다.
4. 테스트 역추적 표와 [테스트 목록](test-cases.md)을 함께 갱신한다. 테스트를 지우면 요구사항 연결 공백을 확인한다.
5. 실행 결과와 revision을 기록하고 host/빌드/flash/보드 증거를 구분한다.
6. README TODO는 작업 요약으로 유지한다. 세부 합격 기준은 이 문서를 참조한다.

예: 부분 프레임 timeout을 바꾸면 REQ-RX-003 → SPEC-TIME →
`CONFIG_RADAR_FRAME_RX_TIMEOUT_MS` → `test_timeout_boundary_for_every_partial_frame` 및 분할/복귀 사례 →
실보드 부분 송신·중단 시험까지 영향 범위를 추적한다. 측정 무수신 timeout을 자동으로 함께 바꾸지 않는다.


### 과거 단계 기록 — FUT-001 partial implementation: STATUS payload decoding

[decode_status_payload](../pc_sim/protocol.py) decodes the 16-byte SPEC-STATUS payload
into four named uint32 counters; invalid lengths raise ValueError.
The caller must validate frame TYPE and CRC first.
Tests in [test_protocol.py](../tests/test_protocol.py): `test_decode_status_payload`,
`test_decode_status_payload_zero`, `test_decode_status_payload_invalid_length` (0/15/17).
2026-11-12: all 5 new cases passed; full Python suite 89 passed and 9 subtests passed.
FUT-001 remains incomplete: stream reception, TYPE/CRC validation and CLI/GUI integration are pending.


### 과거 단계 기록 — FUT-001 부분 구현 추적: PC 프레임 조립

[FrameParser](../pc_sim/frame_parser.py)는 SPEC-WIRE에 따라 분할·연속 입력을 조립하고 LEN·CRC를 검증한다.
TYPE은 그대로 반환하며 STATUS_REPORT 여부와 payload 유효성 검사는 호출자의 책임이다.
[파서 테스트 10개 함수·16개 사례](../tests/test_frame_parser.py)는 정상·모든 INIT 분할 위치·연속 프레임·
CRC/LEN 오류 후 복귀·STX 포함 payload·최대 길이·reset과 통계 보존을 검증한다.
함수별 검증 조건은 [테스트 목록](test-cases.md)의 PC 수신 프레임 파서 절에 연결한다.
2026-11-12 작업본: 파서 16개, 전체 Python 105개와 subtest 9개 통과.
이 결과는 위의 payload-only 단계 이후 추가된 증거다. FUT-001 전체 완료는 아니며
serial 읽기·응답 대기시간·STATUS TYPE 확인·CLI/GUI 연결과 FUT-002 실보드 왕복은 남아 있다.


### 과거 단계 기록 — FUT-001 부분 구현 추적: STATUS CLI

[status_client.py](../pc_sim/status_client.py)의 `request_status()`는 단독 소유 포트에서 기존 입력을
비우고 STATUS_REQUEST를 한 번 전송한다. monotonic deadline 내에 CRC·TYPE·16바이트 payload가
유효한 응답을 해석한다. 프레임 오류·다른 TYPE·잘못된 STATUS는 건너뛰고 대기를 연장하지 않는다.
포트 열기·입력 버퍼 정리 시간은 대기 예산 밖이다. request ID가 없어 지연된 이전 응답 식별은 보장하지 않는다.
`main()`은 포트 수명·출력·종료 코드(성공 0, 통신 실패 1, 인자 오류 2)를 담당한다.
[CLI 21개 테스트](../tests/test_status_client.py)는 SPEC-WIRE/STATUS와 위 deadline·오류 계약을 검증한다.
이 단계는 이전의 CLI 미구현 상태를 대체한다. GUI 연결 및 FUT-002 실보드 왕복은 남아 있다.


### 과거 단계 기록 — FUT-001 GUI 수동 STATUS 조회 구현

[gui_worker.py](../pc_sim/gui_worker.py)의 `request_status()`는 명령 큐로 요청하고 기존 worker가
단독 소유 데이터 포트에서 프레임을 송수신한다. 응답 대기는 2초, 중복 요청은 거부하며 timeout은
측정 송신을 멈추지 않는다. 전체 파싱과 TYPE/16바이트 검증 후 Qt signal로 통계를 전달한다.
[gui.py](../pc_sim/gui.py)는 통계·성공 시각·대기/실패를 표시하고 연결 해제 시 요청을 취소한다.
실패 시 이전 값을 유지하고 새 연결에서는 지운다. 자동 조회와 request ID 상관관계는 제공하지 않는다.
[test_gui.py](../tests/test_gui.py)의 네 STATUS 사례는 REQ-GUI-001 및 FUT-001의 UI·worker 연결을 검증한다.
FUT-001의 CLI·GUI 수동 조회 경로는 구현됐으며 FUT-002 실보드 왕복은 사용자 요청으로 연기했다.


### Handler 명령·측정 경유 검증 보강

소스: [handler 테스트](../fcc_app/tests/test_frame_handler/src/main.c).

| 테스트 함수 | 요구사항 | 검증 |
| --- | --- | --- |
| `test_init_command_resets_tracking` | REQ-CTL-001 | INIT으로 트랙·감시 초기화, 새 측정 재감시, 모사 통계 STATUS 보존 |
| `test_reset_command_resets_tracking` | REQ-CTL-001 | RESET으로 트랙·감시 초기화, 새 측정 재감시, 모사 통계 STATUS 보존 |
| `test_valid_measurements_arm_refresh_and_expire` | REQ-CTL-002 | 첫 유효 측정 감시 시작·갱신·1000ms 경계·1회 초기화 |
| `test_invalid_measurements_do_not_arm_timeout` | REQ-CTL-002, REQ-MEA-001 | 길이 오류·거리 0이 감시를 시작하지 않음 |
| `test_invalid_measurements_do_not_refresh_timeout` | REQ-CTL-002, REQ-MEA-001 | 잘못된 길이·거리 0이 기존 트랙·수신 시각을 변경하지 않음 |
| `test_gate_rejection_refreshes_receive_timeout` | REQ-CTL-002, REQ-TRK-002 | CONFIRMED에서 gate 거부는 COASTING으로 전이하고 수신 시각 갱신 |
| `test_timestamp_rejection_refreshes_receive_timeout` | REQ-CTL-002, REQ-TRK-003 | 중복·역행 timestamp는 트랙을 유지하지만 수신 시각 갱신 |

신규 7개를 포함해 handler host 17개 통과. 기존 CTL-002의 측정 경유 검증 공백을 보강했다.
CTL-001은 명령 경유 트랙 초기화와 모사 통계 응답 보존을 확인했으며 실제 processor atomic
카운터와 handler를 함께 실행하는 host 통합 검증은 남아 있다. 이후 실보드 시험에서는 INIT/RESET 후 실제 카운터 보존을 관측했다. 모든 RTOS 경쟁 조건을 검증한 것은 아니다.


## 이전 STATUS 실보드 결과 (16바이트 규격)

`2aa57d9`에서 CLI·GUI STATUS 왕복, CRC/LEN/부분 프레임 timeout 통계 증가,
INIT/RESET 통계 보존, 추적 중 조회와 무수신 timeout을 확인했습니다.
[2026-11-12 시험 결과·원시 로그](hardware-test-2026-11-12-status.md)를 참고합니다.
이 기록은 이전 STATUS 실보드 미검증/연기 상태를 대체합니다. 강제 링 overflow·UART overrun·부하 시험은 남아 있습니다.


### 2026-11-14 REQ-RX-002 / FUT-008 정책별 host 검증

[RX 테스트](../fcc_app/tests/test_uart_rx/README.md)는 drain 21·discard 18, 총 39회 실행한다.
`test_discard_drops_complete_frame_and_recovers`와 `test_discard_drops_completable_partial_frame`를
REQ-RX-002에 연결한다. 공통 16개는 두 정책 모두에서 검사한다.
Overflow·timeout Kconfig 분리와 host 경계 시험은 완료됐으며 실보드 강제 overflow·자원 검증은 남아 있다.


## 2026-11-14 Kconfig 실보드 회귀

`1e956c5`의 DRAIN·DISCARD 펌웨어를 각각 플래시하고 UART·STATUS·오류 처리 16개 조건씩 통과했습니다.
기본 DRAIN으로 복원하고 STATUS 응답까지 확인했습니다.
[결과와 원시 로그](hardware-test-2026-11-14-kconfig.md).
강제 링 overflow의 정책별 동작·UART 하드웨어 오류·부하 시험은 미검증이며 GUI는 이번에 재검증하지 않았습니다.


## 각도 정규화 후속 검증

`wrap_angle_rad`는 fmodf와 1회 범위 보정으로 (-π, π]를 유지한다.
Tracking의 경계·다중 회전·큰 유한값 시험과 비정상 innovation covariance 보정 생략 시험을 추가했다.
NaN·±Inf 각도 입력의 내부 전제 위반은 Python 자식 프로세스 assert 시험으로 확인한다.
[일반·디버그 실보드 결과](hardware-test-2026-11-14-fmodf.md). 큰 float와 NaN/Inf의 직접 주입은 host 범위다.

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

## 확장 STATUS host·실보드 검증

28바이트 응답에 UART overflow·폐기 카운터 세 개를 연결했다.
handler 17개, 전체 C 103회, Python 160개 + subtest 9개 및 펌웨어 빌드 통과.
구형 16바이트는 PC에서 지원하되 추가 필드는 미지원으로 표시한다.
확장 규격의 실보드 UART 18개 조건과 CLI·GUI 왕복도 통과했다. [확장 STATUS 실보드 결과](hardware-test-2026-11-14-status-extended.md).
새 UART 카운터는 0 전송을 확인했으며 실제 overflow 증가·복구 시험은 별도다.

## 로깅 일반·디버그 실보드 회귀

FUT-009 기본 레벨·문장 출력과 REQ-STA-002/REQ-CTL-001/002 통신 회귀를 확인했다.
`67d247b` 일반·디버그 각 30조건 통과. [증거·최초 판정 실패와 재시험·한계](hardware-test-2026-11-14-logging.md).
강제 로그 포화와 IRQ 경쟁 조건 전수 검증은 포함하지 않는다.

## 로그 포화·측정 부하 후속 검증

FUT-009의 로그 유실 관측과 부하 종료 후 복귀를 실보드에서 확인했다.
STATUS 122개 wire 응답의 CRC·길이·7개 통계도 확인했다.
[시험 조건·한계·원시 기록](hardware-test-2026-11-14-log-stress.md). 최대 지속 처리율 보장은 아니다.

## TRACK_REPORT 측정 경로 host 시험 추가 (2026-11-16)

FUT-003 / V1-1의 일부인 측정 결과 인코딩·handler 제출 경로에
[10개 host 사례](../fcc_app/tests/test_frame_handler/README.md#track_report-host-coverage-2026-11-16)를 추가했다.
handler 전체 27개 통과. 이는 진행 중인 구현의 검증이며 v1 결과 왕복 완료 판정이 아니다.
제어/timeout 이벤트 보고·PC 수신·GUI/정량 평가·실보드 검증은 남아 있다.

레이더 처리 상태는 `frame_handler.radar`가 소유하며 `radar_processor`에서만 갱신한다.
측정 결과와 timeout 발생 여부를 반환하고 보고서 송신 연결은 handler가 담당한다.
분리 후 host 29개 통과; 제어/timeout 이벤트 송신 기능은 추가하지 않았다.

## Timeout/예측 API 역할 분리 — 2026-11-18

`radar_processor_is_timed_out(const ...)`는 판정만 하며 handler가 초기화·로그·보고를 담당한다.
RX는 내부 `rx_frame_processor_is_timed_out()`으로 판정하고 공개 `rx_frame_processor_handle_timeout()`에서 파서 초기화·통계 증가를 수행한다.
`track_predict()`는 `TrackPredictResult`의 PREDICTED/EMPTY/TIME_REJECTED/RESET을 반환한다. 예측·소실·시간 정책은 유지하며 호출자가 상태를 다시 읽어 반환 이유를 추론하지 않는다. 이 enum은 wire enum과 독립적이다.

<a id="spec-eval"></a>
## SPEC-EVAL — CSV와 오프라인 평가

- 새 실행마다 고유 디렉터리에 metadata.json, samples.csv, reports.csv, wire.csv, status.csv를 생성한다. 기존 경로는 덮어쓰지 않는다. 미리보기는 mode=preview로 표시하고 보드 평가에서 거부한다.
- GUI의 송신 중지는 수집 중지가 아니다. timeout 보고를 계속 기록하고 재시작/연결 종료에서 이전 파일을 닫는다. `closed`는 파일 종료 여부이며 시험 합격 플래그가 아니다.
- ESTIMATE_VALID가 있고 estimate_timestamp_ms와 송신 sample timestamp가 일치하는 유일 시각만 평가한다. 무효·대응 불가·반복 추정 시각은 별도 집계하고 제외한다. 중복 송신 timestamp는 모호하므로 평가를 거부한다.
- STARTED마다 2000ms warm-up을 다시 제외한다. 거리 오차는 m, 각도 오차는 [-π,π) 최단 차이(rad). RMSE baseline은 같은 시각의 wire 양자화된 측정값이다.
- PC roundtrip은 송신 직전~해당 입력 보고 수신까지 같은 monotonic 시계로 계산한다. USB/OS·read polling·CSV flush 비용을 포함하며 MCU 처리 지연을 의미하지 않는다.
- 시나리오 기본값: seed42, 10Hz 센서 시간축 120개, 1000m에서 -5m/s, 고정 15도. normal 거리 잡음0, noisy/dropout 표준편차8m. dropout은 센서 시각 [4000,5500)ms를 송신하지 않는다. 모든 시나리오 각도 잡음0.
- 실제 송신은 늦어져도 따라잡기 burst를 하지 않는다. 센서 시간축 10Hz와 실제 벽시계 송신률은 구분한다. 세션 ID/INIT 요청 상관관계 보장은 없으며 미수신 시각의 추정값을 만들어 점수를 내지 않는다.

[재현 명령](../pc_sim/README.md#csv-수집과-정량-평가) · [실보드 결과와 제한](hardware-test-2026-11-18-evaluation.md).

## 소비 스레드 정지 기반 포화 검증 — 2026-11-18

기본 DRAIN 실보드에서 강제 링 overflow·RX 큐 포화/stale 폐기·STATUS TX 큐 포화와 각 단계 정상 추적 복귀를 확인했습니다. 일반 이미지로 복원했습니다. [결과·실패 이력·제한](hardware-test-2026-11-18-transport-stress.md). TRACK TX 포화·제어 유실/순서·지속 부하/자원 시험은 남았습니다.
