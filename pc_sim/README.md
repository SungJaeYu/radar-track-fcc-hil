# PC 레이더 시뮬레이터 · 보드 시험 도구

PC 측정 생성과 UART 시험 도구를 이 폴더 하나로 통합했습니다. 자동 테스트는 루트의 `tests/`에 있습니다.
모든 송신기는 현재 펌웨어의 CRC-16/IBM-3740 규격을 공유합니다. 이전 XOR 프레임은 호환되지 않습니다.

## 역할 경계

이 도구는 **추적 전 레이더 탐지 측정 출력**을 모사합니다.
PC에서 추적을 완료한 트랙이나 안정적인 표적 ID를 보드에 제공하는 구조가 아닙니다.
`Target.target_id`는 PC 내부 정답 식별자이며 현재 `Measurement`와 UART payload에는 없습니다.
향후에도 정답 ID는 PC 평가용으로 분리하고, 측정 연관·트랙 생성·`track_id` 부여는
펌웨어가 담당합니다. 다중표적 연관과 ID 관리는 아직 구현 전입니다.
[시스템 역할과 ID 정책](../docs/system-roles.md)을 참고하세요.

## 데스크톱 GUI

`python -m pip install -r pc_sim/requirements-gui.txt`로 선택 의존성을 설치하고,
저장소 루트에서 `python -m pc_sim.gui`로 실행합니다.
포트 선택·시작/중지·실시간 그래프·보드 콘솔 사용법은 [GUI 안내](GUI.md)를 참고하세요.
기존 CLI와 펌웨어는 그대로 사용할 수 있습니다.

## 파일 역할

- `frame_sender.py`: 문자열을 담은 프레임 하나 송신. 기본 TYPE=1로 필터 초기화에 사용. TYPE=5 RESET도 지원.
- `radar_sender.py`: 거리·방위각 측정값을 생성하고 연속 송신. `--port` 생략 시 출력만 수행.
- `protocol.py`: 공통 프레임 생성·CRC-16·라디안 변환·STATUS payload 해석.
- `frame_parser.py`: 수신 프레임 조립과 LEN/CRC 검증.
- `status_client.py`: STATUS 요청·응답 CLI.
- `recording.py`: 세션별 CSV 기록과 시각 정렬 평가.
- `evaluate.py`: 정상·잡음·연속 누락 실보드 수집 및 오프라인 RMSE CLI.
- `gui.py`, `gui_model.py`, `gui_worker.py`: 화면·측정 생성·포트 소유 및 STATUS 수신.
- `targets.py`: 등속 표적, 잡음, 미탐지, 클러터 모델.
- `run_sim.py`: 표적 시나리오를 현재 규격으로 송신하거나 프레임만 출력.

프레임: `7E | payload 길이 | TYPE | payload | CRC_H | CRC_L`.
레이더 TYPE은 `2`, payload는 big-endian `uint32 timestamp_ms + uint16 range_m + int32 azimuth_sc`의 10바이트(`struct.pack(">IHi", ...)`)입니다.
`--azimuth`는 degree가 아닌 int32 semicircle 값(-2147483648..2147483647)을 직접 받습니다.
변환식은 `azimuth_rad = azimuth_sc × π / 2³¹`이며, `1073741824`는 90°, `-1073741824`는 -90°입니다.
기본값 `178956971`은 약 **15°**입니다(`value × 180 / 2³¹`). CLI 도움말도 같은 단위를 표시하며 FCC는 수신 후 라디안으로 변환합니다.

## 실행

저장소 루트에서 실행합니다. 기존 `tools/hil/` 명령은 아래 `pc_sim/` 경로로 바뀌었습니다.

```bash
source ~/zephyrproject/.venv/bin/activate

# 수동 초기화만 할 때 (아래 연속 송신기는 시작 시 자동 INIT)
python pc_sim/frame_sender.py init --type 1 --port /dev/cu.usbserial-A5069RR4

# 잡음 없이 1000m부터 초당 20m씩 접근, 10Hz로 20개 송신
python pc_sim/radar_sender.py --port /dev/cu.usbserial-A5069RR4 --hz 10 --count 20 --noise 0

# 보드 없이 프레임 출력 확인
python pc_sim/radar_sender.py --count 3 --noise 0
```

포트 이름은 연결 환경에 맞게 바꾸세요. 송신 스크립트는 실행마다 timestamp를 0부터 시작하므로,
매 실행 시 INIT을 자동 전송해 이전 트랙을 초기화합니다.
결과는 ST-Link 콘솔(`radar-console`)에서 확인합니다. USB-UART 포트는 다른 모니터에서 열지 마세요.

## 자동 검증

```bash
python -m pytest
```

`tests/`만 실행하며 보드 연결이나 flash를 수행하지 않습니다.
펌웨어의 [UART 복구](../fcc_app/tests/test_uart_rx/README.md)와
[Tracking C 테스트](../fcc_app/tests/test_tracking/README.md)는 별도 CMake/CTest 명령으로 실행합니다.

## 시나리오 실행

```bash
# 보드 없이 단일 표적 측정 출력 (실시간 대기 없음)
python -m pc_sim.run_sim --duration 1 --noise 0 --azimuth-noise 0

# 시작 시 INIT을 자동 전송한 뒤 시나리오 실행
python -m pc_sim.run_sim --port /dev/cu.usbserial-A5069RR4 --duration 10

# 다중표적·클러터는 PC 출력 전용
python -m pc_sim.run_sim --scenario two_crossing --clutter 0.5 --duration 1

# 미탐지 모델: 각 표적을 스캔마다 80% 확률로 탐지
python -m pc_sim.run_sim --pd 0.8 --duration 1
```

- `--port` 생략은 출력 전용이며, 보드 동작을 모사하는 mock 추적기가 아닙니다.
- 이전 `--mock`은 포트 생략으로, `--serial`은 `--port`로 대체했습니다.
- `radar_sender.py`는 거리 변화율을 직접 지정하는 간단한 송신기입니다.
  `run_sim.py`는 직교좌표 표적 운동을 극좌표 측정으로 변환하는 시나리오 실행기입니다.
  둘 다 같은 `protocol.py`를 사용하며 서로 다른 통신 규격을 유지하지 않습니다.
- 현재 보드에는 다중 측정 데이터 연관이 없으므로 UART 모드는 단일 표적·클러터 0만 허용합니다.
  여러 표적의 동일 timestamp 측정을 보내면 첫 측정 이후 중복 timestamp로 거부될 수 있습니다.
- 센서 모델의 거리(m)는 정수로 반올림하고, 방위각(rad)은 signed int32 semicircle로 변환합니다.
  반올림 후 거리 1..65535m 밖의 측정은 이유를 출력하고 건너뜁니다.
- 모델의 앙각·도플러는 PC 내부 물리량이며 현재 10바이트 payload에는 없습니다.
- 기본 시나리오는 `single_approach`, 탐지확률 1, 클러터 0입니다.
  `--noise`는 거리 표준편차(m), `--azimuth-noise`는 방위각 표준편차(rad)입니다.
- 이 두 송신기는 이진 응답을 수집하지 않습니다. TRACK 수집·RMSE에는 아래 평가 CLI를 사용합니다.
- 두 연속 송신기 모두 실행마다 `7e 01 01 00 c8 9d` INIT을 한 번 보낸 뒤 timestamp를 0부터 시작합니다.
  출력 전용 모드에도 INIT이 먼저 표시됩니다. INIT은 측정 개수(`--count`)에 포함되지 않습니다.
  `flush()`는 송신 완료 대기이며 보드의 처리 완료 응답은 아닙니다.

## 수신 버퍼와 부하 시험

현재 펌웨어 RX 링버퍼는 256바이트 중 255바이트를 저장합니다.
overflow 시 기본 drain 정책은 저장된 데이터로 완성 가능한 프레임을 처리하고,
복구 중 새 입력과 남은 부분 프레임은 버립니다.

`--hz`는 측정 생성·송신 주기를 정하는 옵션이며 보드의 처리 완료를 기다리는 기능은 아닙니다.
송신 속도를 높였다는 사실이나 PC 출력만으로 overflow 발생·복구를 판정하지 않습니다.
보드의 복구 로그와 이후 정상 측정 처리를 확인하는 [보드 시험 절차](../docs/board-bringup.md)를 참고하세요.
STATUS_REPORT로 overflow 횟수와 소프트웨어 폐기 바이트를 조회할 수 있습니다.
하드웨어 유실과 parser가 소비한 부분 프레임은 제외하므로 전체 통신 손실량은 아닙니다.

## 통합과 이력

2026-11-17 수동 도구는 `small_byte_test.py`에서 `frame_sender.py`로 이름을 변경했습니다. 기존 명령의 파일 경로만 바꾸면 TYPE·포트·payload 옵션은 그대로 사용할 수 있습니다.

이전 XOR 규격은 현재 CRC-16 펌웨어와 호환되지 않습니다.
이전 구현은 Git 이력에 남아 있으며, 구버전과 신버전 송신기를 섞어 사용하지 않습니다.
현재와 맞지 않는 과거 설계·계획 문서는 제거했습니다. 현재 실행 방법은 이 문서를 따릅니다.


## STATUS 진단 요청

저장소 루트에서 실행합니다. GUI·송신기를 중지해 데이터 포트를 단독으로 사용합니다.
보드 TX(PC6) → USB-UART RXD 연결도 필요합니다.

```sh
python -m pc_sim.status_client --port PORT --timeout 2
```

기본 baud는 115200이며 `--baud`로 지정할 수 있습니다. 요청 payload는 `00`입니다.
이 명령은 INIT/RESET을 보내지 않으며, 정상 응답을 받으면 CRC errors, Length errors,
Frame timeouts, Queue drops와 링 overflow·ISR 폐기 바이트·복구 폐기 바이트를 출력합니다.

- 송신 직전 기존 수신 버퍼를 비운 뒤 한 번 요청합니다. 포트는 독점 사용해야 합니다.
- 프레임을 분할·연속 조립하고 CRC/LEN 오류, 다른 TYPE, 길이가 잘못된 STATUS는 건너뜁니다.
- `--timeout`은 입력 버퍼 정리 후 요청 쓰기부터 응답 처리까지의 전체 대기 예산입니다.
  포트 열기·버퍼 정리 시간은 제외하며 OS 지연을 포함한 엄격한 실시간 보장은 아닙니다.
  반복 입력으로 종료 시각을 연장하지 않고 각 read/write에도 timeout을 설정합니다.
- 성공은 종료 코드 0, 응답 timeout·시리얼 오류는 1, 인자 오류는 2입니다.
- 포트는 종료 시 닫습니다. 프로토콜에 요청 ID가 없어 늦게 도착한 이전 응답과 완전한 구분은 불가능합니다.
- parser 자체에는 바이트 간 timeout이 없습니다. 이 CLI는 거래 전체 timeout으로 대기를 종료합니다.
- STATUS는 오류 통계이며 실제 BIT 결과는 아닙니다. [실보드 CLI·GUI 왕복](../docs/hardware-test-2026-11-12-status.md)을 확인했습니다.

기존 `python pc_sim/frame_sender.py status --type 3 --port PORT`는 요청만 보내는 도구입니다.
GUI의 STATUS 탭에서도 수동 조회할 수 있습니다. 연결 중에는 CLI가 같은 포트를 열지 않도록 합니다.

## TYPE 요청·보고 구분

| 값 | 규격 상수 | 역할 |
| --- | --- | --- |
| 0x01 | `FRAME_TYPE_INIT` | FCC 추적 초기화 |
| 0x02 | `FRAME_TYPE_MEAS_REPORT` | 레이더 시뮬레이터 → FCC 측정 보고 |
| 0x03 | `FRAME_TYPE_STATUS_REQUEST` | PC 진단 도구 → FCC 상태 조회 |
| 0x04 | `FRAME_TYPE_COMMAND` | 명령 예약, 동작 미구현 |
| 0x05 | `FRAME_TYPE_RESET` | FCC 추적 리셋 |
| 0x06 | `FRAME_TYPE_STATUS_REPORT` | FCC → PC 상태 보고, 32바이트 payload; PC는 구형 16/28바이트 응답도 지원 |
| 0x07 | `FRAME_TYPE_TRACK_REPORT` | FCC → PC 추적 보고, 32바이트 payload; PC 디코더·GUI 표시 지원 |

기존 0x01~0x05 값과 payload는 유지합니다. GUI/CLI의 측정 생성은 공통
`protocol.py`를 통해 MEAS_REPORT를 보냅니다. 주기적 측정 방식이므로 MEAS_REQUEST는
추가하지 않습니다. STATUS_REQUEST는 STATUS_REPORT를 큐에 제출하며 DEBUG 콘솔에는 접수만 기록합니다.
응답 payload는 CRC 오류·길이 오류·부분 프레임 timeout·큐 드롭·링 overflow 횟수·ISR 폐기·복구 폐기·오래된 측정 폐기 순서로
big-endian uint32 여덟 개(총 32바이트)를 담습니다.
RX의 일곱 카운터는 atomic 조회하고 마지막 값은 handler 소유 상태에서 읽지만 전체의 동일 시점 snapshot은 보장하지 않습니다.
보드의 단순 `TYPE_MEAS`/`TYPE_STATUS` 로그는 제거했습니다. 측정 상세는 디버그 빌드에서만 출력하며, 통계는 이진 응답으로 확인합니다.
`frame_sender.py`의 선택지는 구현된 수신 경로 1~5로 유지합니다.


## TRACK_REPORT 지원 상태

펌웨어는 신선한 유효 측정마다 TYPE=7을 TX 큐에 제출합니다. [32바이트 배치와 동작](../docs/requirements-traceability.md#spec-track-report)은 펌웨어에 구현되어 host 검증을 마쳤습니다.
`protocol.decode_track_payload()`는 32바이트를 immutable `TrackReport`로 해석합니다. Python enum은 `TrackState`, `TrackReportResult`, `TrackReportFlag`이며 C wire 값과 일치합니다.
길이·미지원 enum·reserved byte/bit·비유한 float·상태/유효 플래그 불일치·무효 추정값의 비영 필드를 거부합니다. 플래그가 있으면 timestamp/추정값 0도 유효합니다.
GUI worker는 하나의 지속 파서로 STATUS와 TRACK을 TYPE별로 분기합니다. STATUS 요청/timeout 때 TRACK 부분 프레임을 지우지 않습니다. [GUI 표시·STALE 정책](GUI.md#track-수신과-유효성).
로그 부하 시험기는 TRACK의 CRC·길이만 검사합니다. STATUS CLI는 TYPE=6만 조회하며 TRACK 수집 도구가 아닙니다. `evaluate` CLI와 GUI CSV 기록, `recording.evaluate_session()` 정량 평가를 지원합니다.
`radar_sender.py`와 `run_sim.py`는 송신 도구이며 반환 결과를 수집하지 않습니다. 기존 도구의 실행 성공은 TRACK 왕복 성공을 뜻하지 않습니다.
과거 설정별 보드 결과는 [검증 이력](../docs/verification-history.md)을 참고하세요.

## STATUS_REPORT payload

| 바이트 offset | 필드 | 의미 |
|---|---|---|
| 0 | crc_errors | CRC 오류 |
| 4 | length_errors | 프레임 길이 오류 |
| 8 | frame_timeouts | 부분 프레임 timeout |
| 12 | queue_drops | RX → handler 큐 제출 실패(-ENOMSG) |
| 16 | ring_overflow_events | 링 저장 실패로 복구에 진입한 횟수 |
| 20 | rx_dropped_bytes | ISR이 FIFO에서 읽고 링에 저장하지 못한 바이트 |
| 24 | recovery_discarded_bytes | 복구 시 링에 남아 폐기한 미소비 바이트 |
| 28 | stale_measurements | handler가 나이 초과로 폐기한 측정 수; UINT32_MAX 포화 |

모두 big-endian uint32이며 전체 wire 프레임은 37바이트입니다.
offset 20·24의 바이트 카운터는 하드웨어 유실이나 parser가 이미 소비한 부분 프레임을 포함하지 않습니다.
새 PC는 구형 16/28바이트도 수용하고, 없는 필드는 CLI `unsupported`·GUI `미지원`으로 표시합니다.
새 펌웨어는 32바이트만 송신하므로 구형 PC는 업데이트해야 합니다. 다른 payload 길이는 거부합니다.
이는 길이로 구분하는 명시적 세 길이 호환이며 임의 확장·버전 협상은 지원하지 않습니다.

필드를 바꿀 때 C의 `status_report`/`status_message_encode()`와 Python `protocol.py`의
`STATUS_FIELDS`를 함께 수정합니다. Python 길이·디코딩과 CLI/GUI 행은 이 목록을 공유합니다.
C의 `STATUS_REPORT_FIELD_COUNT`도 갱신합니다. payload 길이는 필드 수 × 4바이트로 계산합니다.
정적 assert는 현재 all-uint32 구조체 크기와 payload 길이 일치 및 최대 payload 용량을 검사합니다.
구조체 멤버 수를 자동으로 세는 것은 아니며, 타입·패딩이 바뀌면 크기 검사도 재검토합니다.
이 검사는 필드 순서나 인코더의 기록 누락을 보장하지 않으므로 고정 바이트열 테스트를 유지합니다.
C endian 쓰기는 `common/byte_order.h`를 재사용합니다. 생성기는 사용하지 않습니다. 현재 STATUS가 최대 payload 32바이트를 모두 사용하므로 다음 필드 추가는 프레임 용량/버전 정책부터 검토해야 합니다.
고정 기대 바이트열·C→Python 상호운용·길이 오류·구형 응답·CLI/GUI 테스트도 함께 갱신합니다.
28바이트 응답의 [실보드 UART·CLI·GUI 왕복](../docs/hardware-test-2026-11-14-status-extended.md)을 확인했습니다.
강제 overflow에 의한 새 카운터 증가 시험은 남아 있습니다.

## 로깅 전환 후 보드 사용

일반 INFO 빌드는 측정 상세를 출력하지 않습니다. 상세 관측에는 `debug.conf`를 적용합니다.
STATUS는 두 설정 모두 여덟 통계를 이진 응답으로 반환합니다.
[일반·디버그 실보드 회귀](../docs/hardware-test-2026-11-14-logging.md)는 각 30조건 통과했습니다.
GUI 화면은 해당 로깅 회귀에 포함하지 않았습니다. 이후 [로그 포화·측정 부하 시험](../docs/hardware-test-2026-11-14-log-stress.md)에서 STATUS 유지·유실 알림·복귀를 확인했으며 장시간 검증은 남아 있습니다.

## CSV 수집과 정량 평가

GUI를 닫아 UART를 단독 사용한 뒤 저장소 루트에서 실행합니다. `PORT`를 실제 어댑터 경로로 바꿉니다.

```sh
python -m pc_sim.evaluate run --port PORT --scenario normal --output runs
python -m pc_sim.evaluate run --port PORT --scenario noisy --output runs
python -m pc_sim.evaluate run --port PORT --scenario dropout --output runs
python -m pc_sim.evaluate report runs/SESSION --warmup-ms 2000
```

기본값은 seed42·센서 시간축 10Hz·120개, 초기 1000m·-5m/s·고정 방위각15도입니다.
normal 거리 잡음0, noisy/dropout 표준편차8m이고, dropout은 [4000,5500)ms 측정을 송신하지 않습니다.
각도 잡음의 기본값은 0입니다. CLI는 INIT 초기화 보고를 확인하고 시작하며 마지막 timeout·STATUS·RESET도 수집합니다.
`--count`, `--hz`, `--warmup-ms`, `--range-noise`(m), `--azimuth-noise-deg`(도), `--seed`로 바꿀 수 있습니다. dropout은 최소 9초·5Hz가 필요합니다.
실제 송신은 PC 대기/USB 비용으로 센서 시간축보다 느릴 수 있으며 catch-up burst를 보내지 않습니다.

실행마다 시간·고유 접미사를 붙인 폴더를 만들며 기존 경로를 덮어쓰지 않습니다. 기본 `runs/`는 Git에서 제외합니다.

| 파일 | 내용 |
|---|---|
| metadata.json | 설정, board/preview 모드, 시작 시각, 파일 종료 여부 |
| samples.csv | 센서 시각, 합성 정답, wire 양자화 측정값, 송신 직전 PC monotonic 시각, 프레임 hex |
| reports.csv | 수신 TRACK 필드와 PC monotonic 시각 |
| wire.csv | TX 프레임·RX 원시 chunk hex와 PC monotonic 시각 |
| status.csv | 수신 STATUS 통계; 구형 미지원 필드는 빈칸 |
| evaluation.json | 평가 CLI가 저장한 RMSE·개수·순번 관측·PC 왕복 시간 |

평가는 유효하고 송신 sample과 **추정 시각이 일치하는** 보고만 비교합니다. 중복 추정 시각·무효·대응 불가 보고는 별도 집계하며 STARTED 후 2초를 매번 제외합니다. 같은 시각의 측정값이 baseline입니다. 각도 오차는 최단 wrap 차이입니다.
미리보기는 보드 성능 평가에서 거부합니다. `closed=true`는 파일을 닫았다는 뜻이며 시험 합격을 뜻하지 않습니다. 예외 종료·빈 기록은 실행 오류와 수집 개수를 함께 확인합니다.
PC 왕복 시간에는 USB·OS·read polling·CSV flush 비용이 포함됩니다. MCU 내부 지연·WCET·50Hz 지속 처리 보장이 아닙니다. 세션 ID가 없어 이전 실행의 지연 보고를 완벽히 구분하지 못합니다.
[실보드 수치·GUI 캡처·제한](../docs/hardware-test-2026-11-18-evaluation.md).


## 잡음 크기·seed 반복 평가

한 번의 8m·seed42 결과를 일반적인 필터 성능으로 해석하지 않도록, 거리·각도 잡음을 한 축씩 바꿔 반복합니다. 펌웨어의 필터·게이트 설정은 고정합니다. 현재 거리 R=25m²(표준편차5m), 각도 R≈0.00121847rad²(표준편차2°)이며 시험 잡음에 맞춰 자동 변경하지 않습니다. Q·R 튜닝 비교는 별도 시험입니다.

| 항목 | 기본 행렬 |
| --- | --- |
| 거리 잡음 표준편차 | 0·2·5·8·15·30m, 각도 잡음0 |
| 각도 잡음 표준편차 | 0·0.1·0.5·1·3°, 거리 잡음0 |
| 난수 seed | 0~9, 조건마다 10개 |
| 총 실행 | 공통 무잡음 조건 중복 제거 후 10조건 × 10seed = 100회 |
| 각 실행 | 센서10Hz·120표본, 초기1000m·-5m/s·고정15°, 초기/재획득 후2초 제외 |

```sh
# UART·파일 생성 없이 모든 설정과 생성 표본을 검증하고 계획 출력
python -m pc_sim.noise_sweep --dry-run

# GUI/다른 CLI의 데이터 포트를 닫고 실행
python -m pc_sim.noise_sweep --port PORT --output runs

# 같은 잡음 행렬에 1.5초 연속 누락 추가
python -m pc_sim.noise_sweep --port PORT --scenario dropout --output runs

# 조건을 줄인 실행 예
python -m pc_sim.noise_sweep --port PORT --range-noise 0 8 30 --azimuth-noise-deg 0 1 3 --seeds 0 1 2

# 거리·각도 잡음을 동시에 주는 개별 실행
python -m pc_sim.evaluate run --port PORT --range-noise 8 --azimuth-noise-deg 1 --seed 7
```

100회 기본 행렬은 센서 구간만 합쳐20분이고 초기화·마지막 수집·호스트 I/O 시간을 더하면 더 오래 걸립니다. 누락 시험은 별도100회입니다. 동일 seed·조건은 같은 측정열을 생성하며, 각도 잡음0에서는 기존 seed42 거리 측정열을 유지합니다. 단위가 `run_sim --azimuth-noise`(rad)와 다르므로 이 명령의 `--azimuth-noise-deg`는 도 단위를 사용합니다.

상위 실행 폴더의 `sweep.json`에 계획·각 실행 경로·결과·조건별 평균/최소/최대를 저장합니다. 반복별 CSV와 `evaluation.json`도 보존합니다. `recorded`는 응답 대응·순번·시각 검사를 통과한 기록이라는 뜻이며, 필터가 모든 조건에서 RMSE를 개선했다는 합격 표시는 아닙니다.

- 거리·각도 각각 측정/추정 RMSE와 최대 절대 오차를 기록합니다. 각도 지표 단위는 rad입니다.
- `estimate_coverage`는 전체 송신 표본 중 RMSE에 실제 사용한 비율입니다. warm-up·무효·중복 등도 분모에 포함되므로 그 자체가 수신 성공률은 아닙니다. `matched_input_reports`/`missing_input_reports`로 응답 대응을 따로 봅니다.
- 게이트 거부율·시각 거부·트랙 시작 횟수·LOST 진입 횟수를 함께 기록합니다. 첫 STARTED도 시작 횟수에 포함되며, 누락 timeout의 EMPTY 복귀를 LOST로 세지 않습니다. 재시작 시각·상태 경로는 `reports.csv`에서 확인합니다.
- 응답 누락·중복·대응 불가·순번 공백/역행·시각 거부 실행은 실패로 표시하고 조건별 성공 집계에서 제외합니다. 실패 개수와 원래 결과는 함께 보존하며, 일부 seed의 실패를 숨기고 전체 조건이 성공했다고 해석하지 않습니다.
- 조건별 평균은 완료된 정상 기록의 **seed별 지표를 동일 가중치**로 평균한 값입니다. 표본 전체를 합친 RMSE나 신뢰구간이 아닙니다. 누락 구간에 가상의 추정값을 채우지 않습니다.
- UART 오류·응답 없음·생성 범위 오류가 발생하면 중단하고 부분 기록을 남깁니다. 입력을 경계값으로 잘라서 계속 실행하지 않습니다.

이 행렬의 자동 검증은 실행기·계산·판정의 host 테스트입니다. 새 잡음 조건의 실보드 성능 수치는 실제로 위 명령을 실행한 CSV로 별도 확보해야 합니다. 기존 [V1 수치](../docs/hardware-test-2026-11-18-evaluation.md)는 그대로 보존합니다.

## 순간 이상치·정상 복귀 실보드 시험

`spike_sweep`는 정상 입력 도중 측정만 크게 바꾼 뒤 원래 입력으로 복귀시키는 별도 시험입니다. 정답의 위치·시각은 그대로 유지합니다. 확률 잡음 행렬과 구분하며, 기본 조건은 정지 표적1000m·방위각0°·잡음0·센서10Hz·120표본입니다. 0부터 센 샘플 인덱스40(4초)에서 주입합니다.

| 대상 | 기본 주입값 |
| --- | --- |
| 거리 오프셋 | ±50·±99·±100·±101·±500m |
| 거리 wire 끝값 | −999m / +64,535m 오프셋으로 실제 측정1m / 65,535m 생성 |
| 각도 오프셋 | ±5·±9.9·±10·±10.1·±45·±180° |
| 지속 길이 | 1표본, 5표본 연속 |
| 대조군 | 오프셋0, 한 번 실행 |
| 총 실행 | 거리12값 + 각도12값에 길이2종을 적용한48회 + 대조군1회 = **49회** |

기본 펌웨어의 게이트100m·10°를 전제로 합니다. 게이트를 바꾼 보드에서는 동일한 기대 판정을 사용하면 안 됩니다. 실행기의 `reference_gates`는 예상 설정이며 보드에서 읽어 검증한 값은 아닙니다. 이후 연속 입력의 게이트 판정은 변하는 예측값의 영향을 받으므로, 명백한 안/밖 값은 첫 주입의 수용·거부를 확인하고 후속 상태는 원시 보고로 기록합니다. 정확히100m·10°인 명목 경계는 wire/float 변환과 예측 오차 영향을 볼 수 있도록 첫 판정 강제 없이 관측합니다. +180°와 −180°는 같은 wire 방향으로 정규화됩니다.

```sh
# 보드·파일 접근 없이49회 계획과 wire 범위를 검사
python -m pc_sim.spike_sweep --dry-run

# 전체 실보드 실행: PORT를 데이터 USB-UART 포트로 변경
python -m pc_sim.spike_sweep --port PORT --output runs

# 짧은 확인: 대조군 + 거리500m + 각도45°, 각각 단발성 = 3회
python -m pc_sim.spike_sweep --port PORT --range-offsets 500 --angle-offsets-deg 45 --lengths 1
```

49회는 측정 구간만9분48초이며 초기화·마지막 수집·호스트 I/O 시간이 추가됩니다. GUI 등 다른 프로그램이 같은 데이터 포트를 열지 않도록 합니다. 펌웨어 알고리즘·Q/R 설정 변경은 없습니다.

**회복 기준:** 기본값은 CONFIRMED·UPDATED·현재 입력 시각과 일치하는 유효 추정이 거리 오차2m 이하·각도 최단 오차0.2° 이하로3표본 연속 나오는 것입니다. 프로젝트 시험용 기준이며 외부 요구사항이나 기존 실측에서 보장된 값이 아닙니다. `--recovery-range-m`, `--recovery-angle-deg`, `--recovery-hold`로 실행 전에 바꾸고 metadata에 남깁니다. 최초 주입 직전3표본도 이 오차 범위에서 정상 추적 중인지 확인합니다.

`sweep.json`을 사용하는 잡음 시험과 달리, 이 실행은 상위 폴더에 **`spikes.json`**을 저장합니다. 각 하위 세션은 같은 CSV·metadata·`evaluation.json` 형식을 사용합니다. 개별 세션을 `python -m pc_sim.evaluate report runs/SESSION`으로 다시 평가하면 spike 항목도 재계산합니다.

- 첫 주입 처리 결과, 주입 구간 게이트 거부 개수, 일반 평가의 전체 트랙 시작/LOST 횟수·응답 대응·순번을 기록합니다.
- 주입 시작부터 실행 끝까지의 최대 거리/각도 추정 오차와 추정 불가 표본 수를 남깁니다. 무효·누락·중복 응답이나 오래된 추정은 회복 연속 횟수를 끊습니다.
- `recovery_delay_sensor_ms`는 **첫 정상 입력으로 돌아온 시각부터, 연속 회복 구간의 첫 표본까지**입니다. 첫 정상 표본부터 안정적이면0ms·`clean_samples_to_recovery=1`입니다. 이를 확인한 마지막 표본의 시각은 `confirmation_sensor_ms`로 구분합니다.
- 회복 평가는 초기/재획득 warm-up 제외를 적용하지 않습니다. LOST 이후 재획득 초반의 나쁜 오차를 제거해서 빨리 회복한 것처럼 보이게 하지 않습니다. 회복을 관측하지 못하면 시간은0이 아니라`null`입니다.
- 시간은 합성 **센서 시간축** 기준입니다. 벽시계 복구시간이나 MCU 처리지연이 아닙니다. 고정한 길이의 실행 내 회복 여부이며 다른 부하·기동의 보장은 아닙니다.
- 응답 대응·순번·시각 검사, 주입 전 정상 추적, 명백한 안/밖 첫 게이트 판정, 회복 기준을 만족해야 해당 실행이 통과합니다. 회복 실패는 기록 후 다음 조건을 계속 시험하고 최종 종료 코드는1입니다. UART 오류는 부분 기록을 보존하고 중단합니다.

**준비·검증 상태:** 실제 C handler/필터의48개 조합을 host에서 실행했고 모사 UART로 주입·기록·재평가·실패 보존을 검증했습니다. 이49회 행렬의 실제 STM32 결과는 아직 없습니다. 기존 잡음100회·누락100회와 별도로 실행하고 폴더 전체를 보존합니다.
