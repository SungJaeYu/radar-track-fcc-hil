# 구조·이름 점검

기준: 2026-11-16, 코드 `87006b0`. 펌웨어 C/헤더 37개, PC 모듈, 테스트·빌드 연결과 기존 Markdown 31개를 점검했다.
**2026-11-17 반영 완료 (`fd96c17`):** 아래 C/PC 이름 표의 변경을 적용했다. wire·알고리즘·시간/초기화 정책은 유지했다.
선택 항목인 Kalman 분산 이름과 PC 모델 변수도 함께 정리했다. 문서/빌드 디렉터리의 선택적 이동과 별도 설계 과제는 남겨 두었다.
동작 결함 전수 감사나 실보드 재검증은 아니다. 규격은 [요구사항 추적표](requirements-traceability.md), 구현 순서는 [v1 기준](roadmap.md#v1-acceptance)을 따른다.

## 판단과 변경 순서

현재 `application / transport / protocol / tracking / common` 구분은 유지하는 편이 좋다.
문제는 폴더가 부족한 것보다 과거 역할을 담은 이름이 남아 있는 점이다.
특히 센서/보드 시계, 측정/추정, 처리 성공/추적 수용, 큐 제출/송신 완료를 구분해야 한다.

1. **의미가 어긋난 변수와 검증 이름**부터 변경한다. `last_measurement_time_ms`, `protocol_frame_is_valid`, 보고서 인자명 등이 해당한다.
2. **공개 API 접두어와 측정 모듈 이름**을 한 번에 맞춘다. 호출부·C 래퍼·host fixture·규격 링크를 함께 바꾼다.
3. **수동 도구·테스트 경로**를 변경한다. 기존 CLI 사용법·CMake·VS Code 컴파일 데이터베이스의 경로까지 갱신한다.
4. **PC 데이터 모델의 거리/잡음 이름**도 이번에 명확히 했다. 지금 UI 구조를 새로 분리할 필요는 없다.

wire TYPE·필드 순서·단위·enum 숫자·시간/유효성 정책은 이름 변경과 별개로 보존한다.

## 적용한 C 이름

아래 경로는 `fcc_app/src/` 기준이다.

| 우선 | 변경 전 위치·이름 | 적용 이름 | 이유·영향 |
| --- | --- | --- | --- |
| 높음 | [tracking/track.h](../fcc_app/src/tracking/track.h) `track.last_measurement_time_ms` | `estimate_timestamp_ms` | 실제로 `track_predict()`가 갱신하며 게이트 거부 후에도 예측 시각이다. 마지막 수용 측정 시각으로 읽으면 오해한다. track·radar_processor·report_sender와 tracking/handler 시험 참조 변경. wire의 동명 필드와 의미도 맞음 |
| 높음 | [application/report_sender.h](../fcc_app/src/application/report_sender.h) `report_sender.report_sequence` | `next_track_report_sequence` | STATUS에는 쓰지 않고 다음 TRACK 보고 시도에 붙일 값이다. wire 구조체의 `track_report.report_sequence`는 이미 부여한 값이므로 유지. 실패·INIT/RESET 보존·순환 시험 필요 |
| 높음 | [protocol/protocol_frame.h](../fcc_app/src/protocol/protocol_frame.h) `protocol_frame_is_valid()` | `protocol_frame_has_valid_length()` | NULL·LEN만 검사하며 TYPE 지원·CRC·payload 의미는 검사하지 않는다. NULL은 false라는 계약 유지. submit·encode·테스트에서 변경 |
| 높음 | [protocol/track_message.c](../fcc_app/src/protocol/track_message.c) `p_track` | `p_report` | 인자 타입은 내부 추적 상태 `struct track`이 아니라 `struct track_report`. 헤더·정의 함께 변경 |
| 높음 | [protocol/status_message.c](../fcc_app/src/protocol/status_message.c) `p_stats` | `p_report` | 실제 인자는 두 통계 소유자의 값을 조합한 `status_report`. `out_stats` 조회 API와 구분 |
| 중간 | [application/frame_handler.h](../fcc_app/src/application/frame_handler.h) `handle_frame()` | `frame_handler_process()` | 같은 공개 API의 `frame_handler_init/reset/submit`와 접두어 통일. RX 큐에 넣는 submit과 이미 꺼낸 프레임 처리도 구분 |
| 중간 | [application/radar_processor.h](../fcc_app/src/application/radar_processor.h) `radar_processor_process()` | `radar_processor_process_measurement()` | 처리 대상이 MEAS_REPORT임을 명시. bool true는 payload 처리 성공이며 gate/time 수용을 뜻하지 않는 계약 유지 |
| 중간 | [protocol/frame_parser.h](../fcc_app/src/protocol/frame_parser.h) `parser_init()`, `parser_feed()` | `frame_parser_init()`, `frame_parser_feed()` | 파일·구조체·공개 함수 접두어 일치. RX·TX/handler fixture·Python C 상호운용 래퍼 참조가 많음 |
| 중간 | [protocol/radar_message.c/.h](../fcc_app/src/protocol/measurement_message.h)와 `decode_radar_measurement()`, `validate_radar_measurement()` | `measurement_message.c/.h`, `measurement_message_decode()`, `measurement_message_validate()` | STATUS/TRACK도 레이더 메시지여서 radar가 너무 넓다. `status_message_encode`·`track_message_encode`와 `모듈_동작` 순서 통일. decoded 타입 `radar_measurement`는 의미가 분명해 유지 가능 |
| 중간 | [transport/uart_tx.h](../fcc_app/src/transport/uart_tx.h) `struct send_frame`, [application/frame_handler.h](../fcc_app/src/application/frame_handler.h) `struct received_frame` | `struct tx_frame`, `struct rx_frame` | 동사형 send와 과거분사 received가 섞였다. 양 방향 wrapper 이름만 통일하고 RX 수신 시각·각 큐/submit은 유지. 공통 `protocol_frame`에 RX 시각을 옮기지 않음 |
| 낮음 | [transport/rx_frame_processor.h](../fcc_app/src/transport/rx_frame_processor.h) `struct rx_frame_stats` | `struct rx_frame_processor_stats` | UART 통계와 소유 모듈을 구분하기 쉬움. STATUS wire의 필드명은 바꿀 필요 없음 |
| 낮음 | [application/radar_processor.c](../fcc_app/src/application/radar_processor.c) `rm` | `measurement` | 공개 타입이 이미 의미를 설명하지만 지역 축약은 디버깅·검색에 불리함 |
| 낮음 | [common/ring_buffer.c](../fcc_app/src/common/ring_buffer.c) `ring_push(..., uint8_t c)` | `byte` | 문자가 아니라 UART 임의 바이트. `out_byte`와 대응 |
| 선택 | [tracking/kalman_cv.h](../fcc_app/src/tracking/kalman_cv.h) `process_noise`, `measurement_noise` | `process_noise_variance`, `measurement_noise_variance` | 현재 값은 표준편차가 아닌 분산이다. PC의 잡음 표준편차와 혼동을 줄임. 수식·초기값·단위는 그대로 유지 |

### 지금 유지할 이름

- `range_m`, `azimuth_rad`, `range_rate_mps`, `azimuth_rate_radps`, `estimate_valid`: 물리량과 역할이 명확하다.
- 추정값 명사는 `estimate`, 유효성은 `estimate_valid`, 시각은 `estimate_timestamp_ms`로 유지한다. `estimated`를 일괄 치환해 섞지 않는다.
- `received_at_ms`: RX 파서 완료 시 보드 uptime이라는 헤더 계약과 맞는다. `input_timestamp_ms`와 합치지 않는다.
- Kalman의 `estimate`, `rate`, `p00/p01/p10/p11`: 거리/각도 공용 축과 표준 행렬 기호이므로 무조건 길게 바꾸지 않는다.
- `ring_init/push/pop`: 이미 작은 모듈 내에서 일관된다. `ring_buffer_*`로 확대하는 것은 선택 사항이고 우선순위가 낮다.
- `ret`, `dt_s`, ISR/CRC/UART/RX/TX: 관례적이거나 단위·문맥이 명확한 약어다.
- Python enum의 `ParserState.WAIT_STX`에는 C처럼 타입 접두어를 중복하지 않는다. C enum 규칙과 언어별로 구분한다.

## PC 이름과 경로

| 우선 | 변경 전 | 적용 이름 | 이유·변경 범위 |
| --- | --- | --- | --- |
| 높음 | [pc_sim/small_byte_test.py](../pc_sim/frame_sender.py) | `pc_sim/frame_sender.py` | 자동 테스트가 아니라 임의 TYPE·문자열 payload 한 프레임을 보내는 CLI. 문서 실행 예제와 Python 테스트의 import/실행 경로 변경 |
| 중간 | [tests/test_hil_protocol.py](../tests/test_frame_sender.py) | `tests/test_frame_sender.py` | 전체 HIL 프로토콜이 아니라 위 수동 송신기의 인자·바이트·INIT/RESET을 검사. `test_small_byte_*`도 `test_frame_sender_*`로 함께 변경 |
| 중간 | [fcc_app/tests/test_kalman](../fcc_app/tests/test_tracking/README.md) | `fcc_app/tests/test_tracking` | 필터뿐 아니라 angle/gating/track/측정 decode까지 검증한다. CMake project·testcase.yaml·문서 명령·로컬 build cache 갱신. 실행 파일 `track_tests`는 유지 가능 |
| 중간 | [pc_sim/protocol.py](../pc_sim/protocol.py) `build_radar_frame()`, `build_radar_frame_from_radians()` | `build_measurement_frame()`, `build_measurement_frame_from_radians()` | STATUS/TRACK와 구분되는 MEAS_REPORT 생성이라는 역할 표시. CLI·GUI·시나리오·상호운용 시험을 함께 변경 |
| 중간 | [pc_sim/gui_model.py](../pc_sim/gui_model.py) `Settings.range_m`, `rate_mps`, `noise_m`, `hz` | `initial_range_m`, `range_rate_mps`, `range_noise_std_m`, `sample_rate_hz` | 초기값·어느 물리량의 변화율인지·표준편차·생성 빈도를 구분. UI 설정 생성과 모델 시험을 함께 변경 |
| 중간 | [pc_sim/gui_model.py](../pc_sim/gui_model.py) `Sample.truth_m`, `measured_m` | `truth_range_m`, `measured_range_m` | 향후 방위각·보드 추정 필드를 추가할 때 거리값임을 명시. GUI 그래프·시험 참조 변경 |
| 낮음 | [pc_sim/targets.py](../pc_sim/targets.py) `radial_velocity()` | `radial_velocity_mps()` | 같은 모델의 `range_m`, `azimuth_rad`와 단위 표기 일치. 센서 모델 호출부 함께 수정 |
| 낮음 | [tests/test_crc_interop.py](../tests/test_crc_interop.py) `test_reserved_status_report_framing` | `test_status_report_framing` | STATUS는 더 이상 예약 TYPE이 아님. 이 시험은 framing 검사이며 실제 payload 검증은 별도 사례라는 경계 유지 |

`pc_sim`은 송신·센서 모델·STATUS 진단·GUI를 모은 현재 단일 PC 도구 위치로 유지한다.
`radar_sender.py`는 간단한 거리 변화 송신기, `run_sim.py`는 표적/센서 시나리오 실행기로 역할이 다르다.
지금 두 파일을 합치거나 `SessionWorker`·`Settings` 같은 문맥 있는 모든 이름을 늘릴 필요는 없다.
실행 옵션 이름까지 바꿀 경우 CLI 호환성 변화이므로 Python 내부 변수 변경과 분리한다.

## 파일 배치·삭제 판단

| 현재 위치 | 판단 |
| --- | --- |
| `fcc_app/src/application/` | `frame_handler`는 큐·분기, `radar_processor`는 추적 상태, `report_sender`는 보고서 구성·제출. 현재 분리를 유지 |
| `fcc_app/src/protocol/` | wire 프레임/CRC 조립·메시지 codec. `track_message`·`status_message`의 배치 적절 |
| `fcc_app/src/tracking/`, `common/` | 추적 계산과 공통 도구 분리 유지. `byte_order.h`는 정수/float 비트 쓰기 역할에 맞음 |
| `fcc_app/` 및 프로젝트명의 FCC | 현재 기능은 측정 기반 추적기다. 경로 변경은 west/CMake/문서/블로그까지 영향이 커서 유지. 역할 문서에서 완성된 레이더 메인 컴퓨터로 확대 해석하지 않도록 설명 |
| `tests/`, `fcc_app/tests/` | PC Python과 C host/보드 시험 구분 유지. 통합 tests 폴더로 전부 옮길 실익이 작음 |
| `fcc_app/tests/common/` | host runner·logging/assert adapter의 공유 위치 적절. suite별 `test_zephyr.h`는 모사 범위가 달라 이름이 같다는 이유로 합치지 않음 |
| `fcc_app/tests/log_stress/` | 시험용 소스·설정·실행기를 함께 유지. 현재 규격과의 호환성은 아래 후속 항목 참조 |
| `EXPLAINER.md` | 이름만으로 설계/사용법 구분이 약하다. 이동한다면 `docs/architecture.md` 권장. 루트 README와 외부 참조를 먼저 확인; 이번에는 경로 유지 |
| `pc_sim/README.md`, `pc_sim/GUI.md` | CLI와 화면 사용법으로 역할이 분명해 유지. GUI.md를 소문자로 바꾸기만 하는 이동은 보류 |
| `docs/hardware-test-*.md`, `docs/test-results/` | 버전별 시험·원시 증거이므로 보존. 묶어 이동하려면 블로그 포함 기존 링크 갱신 필요. 과거 스크립트를 현재 규격으로 덮어쓰지 않음 |
| `docs/structure-review-2026-11-14.md`, `docs/comment-audit.md` | 날짜/대상 범위를 가진 이전 판단으로 유지하고 최신 점검 링크 제공 |
| `build*`, `.vscode/` | 로컬 산출물·개인 설정이며 Git 제외 유지. `build_logging_debug`를 `build_debug`로 바꾸는 것은 선택 사항; 바꾸면 로컬 compileCommands도 갱신 |

삭제가 필요한 중복 제품 소스는 찾지 못했다. 반복된 문서의 시험 이력은 EXPLAINER·CLAUDE·보드 사용 절차에서
줄이고 [검증 이력](verification-history.md)으로 연결했다. 테스트 목록의 과거 개수는 날짜별 기록으로 보존한다.

## 이름 변경과 구분할 설계·계약 후속 항목

- **TrackState의 wire 값:** 현재 enum 선언 순서 0..4를 그대로 보낸다. PC 디코더를 추가하기 전에 명시적 숫자 또는 직렬화 매핑으로 고정하고 검증하는 편이 좋다. 단순 이름 정리와 분리한다.
- **TrackReportResult 의존 방향:** `radar_processor.h`가 wire 보고 enum을 사용한다. 현재 한 결과 경로에서는 유지할 수 있다. 출력 형식이 늘어나면 내부 `TrackUpdateResult`와 wire `TrackReportResult`를 분리하고 report_sender에서 명시적으로 매핑한다. 이름만 바꿔서는 의존성이 사라지지 않는다.
- **init/reset의 의미:** `frame_handler_init()`은 현재 reset과 같고 보고 순번을 지우지 않으므로 최초 zero-init이 필요하다. `frame_handler_reset_tracking()`처럼 범위를 명시하는 것이 낫다. init을 완전 초기화로 바꾸는 것은 재초기화 동작 변경이므로 별도 결정·시험이 필요하다.
- **보고 제출 API:** `report_sender_submit_*()`는 `void`로 실패를 자체 기록하지만 `uart_tx_submit()`은 errno를 반환한다. 현재 계약이 주석에 있으므로 오류라고 보지는 않는다. 호출자가 전송 결과를 판단해야 할 때 반환 계약을 바꾸되 큐 접수/송신 완료를 계속 구분한다.
- **각도 도움말 수정 완료:** 기본 semicircle `178956971`은 유지하고 CLI help의 30°를 실제 값인 15°로 수정했다.
- **공개 주석 수정 완료:** `track_message_encode()`는 원본 보고서 보존을 설명하며 `frame_handler_process()`는 TRACK 제출과 수신 보고 무시를 명시한다.
- **로그 부하 판정 수정 완료:** `check_wire()`는 TYPE별로 나누어 STATUS만 응답 수에 포함하고 TRACK의 CRC·32바이트 길이를 확인한다. host 회귀 12개를 추가했다. TRACK 수치/순번·전달률과 수정 후 실보드 부하는 아직 검증하지 않았다. [현재 제한](../fcc_app/tests/log_stress/README.md#대상-버전과-현재-제한).

## 변경 시 확인할 것

- 변수/API 변경: C 호출부·공개 헤더·테스트 fixture·Python C 래퍼를 `rg`로 찾아 함께 바꾸고 해당 host 회귀를 실행한다.
- 파일 이동: `fcc_app/CMakeLists.txt`, 각 test CMake·testcase.yaml, Python 경로·문서 링크를 갱신한다.
- 파일 이동 후 일반/디버그·host 빌드를 다시 구성한다. 옛 source 경로가 cache에 남으면 새 build 디렉터리로 구성한다.
- 프로토콜 바이트열·순번/초기화·timeout 결과가 그대로인지 확인하고 firmware 일반/디버그 빌드와 필요 범위의 보드 회귀를 구분해 기록한다.
- 과거 증거 문서의 옛 함수명·경로는 당시 사실이면 유지한다. 현재 사용 안내와 구분되는 표지만 추가한다.

## 최초 Markdown 점검 범위와 결과 — 2026-11-16

의존성·빌드 산출물은 제외하고 저장소가 추적하는 기존 Markdown 31개를 대상으로 했다.
아래 “유지”는 누락이 아니라 내용·경로·기록 성격을 확인한 뒤 변경이 불필요하다고 판단한 문서다.

| 문서 | 결과 |
| --- | --- |
| [README](../README.md) | TRACK 구현 경계·모듈 구성·v1 TODO·검증 링크 갱신 |
| [CLAUDE](../CLAUDE.md) | 현재 API 책임·29개 host·PC 미구현 경계, 중복 이력 축소 |
| [EXPLAINER](../EXPLAINER.md) | processor/sender 흐름·순번·유효성·timeout 초기화 범위 갱신 |
| [시스템 역할](system-roles.md) | 펌웨어 TRACK 출력과 PC 평가 구분 |
| [보드 절차](board-bringup.md) | TX 배선·보고/로그 구분·TRACK 검증 공백·과거 이력 링크 |
| [코딩 규칙](coding-standard.md) | 모듈/API·시간/단위 명명 권장, float32 wire 직렬화 반영 |
| [코딩 규칙 감사](coding-standard-audit.md) | 완료된 로깅 TODO 수정, 현재 이름 점검 연결 |
| [주석 감사](comment-audit.md) | 과거 31개 대상 표시·분리된 파일과 float helper 변경 설명 |
| [이전 구조 검토](structure-review-2026-11-14.md) | 당시 판단임을 표시·최신 검토 연결 |
| [요구사항 추적](requirements-traceability.md) | TRACK 규격·REQ-RPT 3개·handler 29개 역추적·v1 목표 경계 |
| [테스트 목록](test-cases.md) | 현재 집계·handler/TX 명령·TRACK 요구사항 연결 |
| [검증 이력](verification-history.md) | 87006b0 host/build 결과와 보드 미검증 구분 |
| [로깅](logging.md) | REGISTER/DECLARE 소유권·TRACK 접수 로그 반영 |
| [PC 안내](../pc_sim/README.md) | 모듈 목록·TYPE=7 지원 차이·로그 부하 검증 상태 |
| [GUI 안내](../pc_sim/GUI.md) | STATUS 통계와 큐 로그 구분·TRACK 표시 미구현 |
| [handler 시험](../fcc_app/tests/test_frame_handler/README.md) | 정상 로그 4줄·processor 사례 이름 명시 |
| [RX 시험](../fcc_app/tests/test_uart_rx/README.md) | 유지: 45개 정책별 범위와 모사 한계 일치 |
| [TX 시험](../fcc_app/tests/test_uart_tx/README.md) | STATUS/TRACK 제출 시험과 PC STATUS 시험 범위 구분 |
| [Tracking 시험](../fcc_app/tests/test_tracking/README.md) | 유지: 17개 + 설정 행렬 20개와 host/Ztest 경계 일치 |
| [로그 부하 도구](../fcc_app/tests/log_stress/README.md) | 현재 TRACK와 호환되지 않는 판정 설명 |
| 과거 에디터 안내(현재 저장소에서 제거) | 파일 이동 후 compile database/cache 갱신 절차 보완 |
| [11-01 보드](hardware-test-2026-11-01.md) | 유지: 당시 결과·로그 링크 |
| [11-12 STATUS](hardware-test-2026-11-12-status.md) | 유지: 16바이트 당시 규격·결과 |
| [11-14 assert](hardware-test-2026-11-14-assert.md) | 유지: 당시 설정·검증 한계 |
| [11-14 fmodf](hardware-test-2026-11-14-fmodf.md) | 유지: 당시 결과·한계 |
| [11-14 게이트](hardware-test-2026-11-14-gating.md) | 유지: 두 설정·판정·복원 기록 |
| [11-14 Kconfig](hardware-test-2026-11-14-kconfig.md) | 유지: 정책별 UART 회귀·강제 overflow 제외 |
| [11-14 단선](hardware-test-2026-11-14-link-loss.md) | 유지: 물리 분리·복원 단계와 판정 |
| [11-14 로그 부하](hardware-test-2026-11-14-log-stress.md) | 유지: 당시 commit·실측률·유실·복귀 증거 |
| [11-14 로깅](hardware-test-2026-11-14-logging.md) | 유지: 최초 실패와 후속 통과 기록 |
| [11-14 확장 STATUS](hardware-test-2026-11-14-status-extended.md) | 유지: 28바이트 결과·강제 overflow 제외 |

2026-11-16 문서 점검 단계에서는 Markdown만 수정했다. 문서 링크·앵커·소스 심볼·테스트 등록 수를 확인하며,
새 기능 테스트·펌웨어 빌드·flash·실보드 시험을 실행한 것으로 기록하지 않는다.

문서 점검 결과: 내부 링크·앵커 검사 통과, handler 등록 29개와 요구사항 역추적 29개 일치.
`pytest --collect-only -q`는 160개를 수집했으며 CTest의 실행 없는 등록 조회는 99개 항목이다.
Tracking의 한 항목 안에서 17개를 실행하므로 C 논리 사례 합계는 115회다. `git diff --check`도 통과했다.

## 적용 후 검증 — 2026-11-17

변경 전후 Python 160개 + subtest 9개와 C host 115회 회귀를 확인했다.
로그 시험기 수정 `838aca6` 후 Python은 12개가 추가되어 172개 + subtest 9개를 통과했다.
일반/디버그 빌드와 여섯 compile database의 현재 경로도 확인했다.
[검증 기록](verification-history.md)에 명령·빌드 메모리·한계를 정리했다. flash·실보드 검증은 수행하지 않았다.

## 전체 Markdown 재검토 — 2026-11-18

기존 Markdown33개와 새 평가 보고서1개를 검토했습니다. 현재 안내의 STATUS32바이트·handler stale 정책·CSV/평가·검증 범위를 갱신했습니다. 날짜별 과거 수치와 당시 미구현 기록은 역사적 증거로 보존했습니다.
이전 이름 변경 표와 당시 테스트 개수도 소급 수정하지 않습니다. 최신 실행 결과는 [검증 이력](verification-history.md), 다음 작업은 [로드맵](roadmap.md#다음-구현-순서)를 따릅니다.

| 문서 | 이번 판단 |
|---|---|
| README.md | 현재 구현 범위·TODO·다이어그램·V1-1/2 검증 상태 갱신 |
| CLAUDE.md / EXPLAINER.md | handler 소유 통계·기준 시각·보고/평가 경로와 최신 시험 개수 갱신 |
| pc_sim/README.md / GUI.md | 16/28/32 호환·CSV 파일·실행 명령·warm-up·정렬·실보드 캡처 추가 |
| docs/requirements-traceability.md | REQ-MEA-002/PC-004, SPEC-TIME/STATUS/EVAL, handler30개 역추적 추가 |
| docs/test-cases.md / verification-history.md | 현재 개수·추가 회귀·최신 실보드 근거 연결 |
| docs/board-bringup.md / system-roles.md | TRACK/CSV/평가 미구현이라는 오래된 안내 수정 |
| docs/logging.md | stale 폐기 경고 제한과 handler 카운터 소유권 반영 |
| fcc_app/tests/test_frame_handler/README.md | 30개 사례, 32바이트 STATUS와 경계 테스트 설명 |
| fcc_app/tests/test_uart_tx/README.md | PC TRACK 디코더 미구현 문구 수정 |
| fcc_app/tests/log_stress/README.md | 과거28·현재32바이트 STATUS 캡처 호환, 보드 부하 재시험 경계 유지 |
| docs/coding-standard.md / coding-standard-audit.md | 기존 소유권·단위·직렬화·테스트 규칙 유지; 새로운 스타일 규칙 불필요 |
| docs/comment-audit.md / structure-review-2026-11-14.md | 날짜/대상 범위가 표시된 과거 감사 결과로 유지 |
| fcc_app/tests/test_uart_rx/README.md / test_tracking/README.md | RX45·Tracking17+20과 모사 한계가 현재와 일치하여 유지 |
| 과거 에디터 안내(현재 제거) | 당시 C 소스 경로·compile database 안내를 확인한 기록 |
| docs/naming-review.md | 이 재검토 기록 추가; 이전 이름 변경 결과 보존 |
| docs/hardware-test-2026-11-01.md | 당시 GUI·RX·burst 증거 유지 |
| docs/hardware-test-2026-11-12-status.md | 당시16바이트 STATUS 증거 유지 |
| docs/hardware-test-2026-11-14-assert.md / -fmodf.md / -gating.md | 각 설정·수치 보호·게이트 당시 증거 유지 |
| docs/hardware-test-2026-11-14-kconfig.md / -link-loss.md | 당시 정책·실제 단선 시험과 미검증 경계 유지 |
| docs/hardware-test-2026-11-14-logging.md / -log-stress.md / -status-extended.md | 당시28바이트·부하·실패/복귀 증거 유지 |
| docs/hardware-test-2026-11-18-track.md | e66515e의64개 판정·29개 보고 기록 유지 |
| docs/hardware-test-2026-11-18-evaluation.md | 신규: 대상 소스 차이/해시·CSV·3시나리오·GUI13개·실패 이력·한계 |

새 Python 모듈은 `recording.py`(Qt 없는 기록·평가)와 `evaluate.py`(시리얼 시나리오 CLI)로 기존 pc_sim에 둡니다. 별도 simulator 디렉터리나 제품 스레드는 만들지 않았습니다. 일반 사용자 기록 runs/는 Git 제외, 선택한 시험 증거만 docs/test-results에 보존합니다.

## 2026-11-21 시간축 이름 정리

위 11월 17일 점검에서 사용한 시간 이름은 현재 펌웨어에서 다음과 같이 구체화했습니다.
`timestamp_ms` → `sensor_time_ms`, `estimate_timestamp_ms` → `estimate_sensor_ms`,
`input_timestamp_ms` → `input_sensor_ms`, `received_at_ms` → `received_uptime_ms`,
`now_ms` → `now_uptime_ms`, `delta_ms` → `prediction_interval_ms`, `dt_s` → `interval_s`.
예측 함수의 목표 시각은 `target_sensor_ms`, 보드의 마지막 유효 수신 시각은
`last_valid_rx_uptime_ms`입니다. 세부 의미와 PC/CSV 이름 대응은
[시간 이름 규칙](coding-standard.md#시간-이름)을 따릅니다. wire와 PC API 이름은 유지합니다.
