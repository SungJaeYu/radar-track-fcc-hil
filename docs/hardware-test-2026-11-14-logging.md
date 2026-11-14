# 로깅 전환 실보드 시험 — 2026-11-14

## 대상

- 펌웨어 revision: `67d247b` (제품 소스 변경 없음).
- STM32F746G-DISCO, USART6 PC6 TX / PC7 RX, 115200 8N1.
- 일반 `build`: 앱 다섯 모듈 INFO, assert 비활성.
- 디버그 `build_logging_debug`: 앱 다섯 모듈 DEBUG, assert 활성.
- 공통: DRAIN, timeout 각 1000ms, 게이트 100m/10°, 로그 버퍼 4096바이트,
  deferred worker·console UART, 공간 부족 시 새 로그 폐기·스레드 대기 없음.
- 데이터 `/dev/cu.usbserial-A5069RR4`, ST-Link `/dev/cu.usbmodem1203`.

## 시험 방법

각 펌웨어를 OpenOCD로 플래시한 뒤 [UART 시험 스크립트](test-results/logging-2026-11-14/uart-check.py)를
`normal` 또는 `debug` 인자로 실행했다. 스크립트는 포트·저장소 경로를 고정한 시험 스냅샷이다.
재사용할 때 환경과 새 출력 경로를 지정해 기존 증거를 덮어쓰지 않는다.
제품 코드를 바꾸거나 특수 로그 부하용 펌웨어를 사용하지 않았다.

INIT 후 첫 측정 전 대기, 반복 STATUS, CRC 변조, LEN 0/33, 부분 프레임 중단,
RESET/INIT 통계 보존, 10Hz 측정 10개와 STATUS 병행, 측정 중단 후 STATUS만 계속 전송,
미구현 COMMAND·미지원 TYPE·거리 0 입력, 이후 STATUS 복귀를 순서대로 확인했다.

## 결과와 증거

| 대상 | 판정 | 증거 |
|---|---|---|
| 일반 INFO | 30개 조건 통과 | [결과·소스/설정/HEX 해시](test-results/logging-2026-11-14/normal/results.json), [콘솔](test-results/logging-2026-11-14/normal/console.log), [TX/RX](test-results/logging-2026-11-14/normal/traffic.json) |
| 디버그 DEBUG | 30개 조건 통과 | [결과·소스/설정/HEX 해시](test-results/logging-2026-11-14/debug/results.json), [콘솔](test-results/logging-2026-11-14/debug/console.log), [TX/RX](test-results/logging-2026-11-14/debug/traffic.json) |

- 두 설정 모두 STATUS 일곱 통계를 수신했고 CRC +1, LEN +2, 부분 timeout +1을 확인했다.
- INIT/RESET 후 누적값 보존, 측정 중 조회와 중단 후 timeout 1회, 이후 STATUS 왕복을 확인했다.
- INFO에서는 측정 상세/STATUS 접수 DEBUG 로그가 없었다.
- DEBUG에서는 측정 입력 10회, 예측 9회, 처리 결과 9회와 STATUS 큐 접수가 출력됐다.
- 트랙 생성과 TENTATIVE → CONFIRMED 전이는 각각 한 번 기록됐다.
- COMMAND 미구현·미지원 TYPE·잘못된 측정 경고가 출력됐고 이후 STATUS는 정상 응답했다.
- 관측한 콘솔 줄은 timestamp·level 접두사를 유지했고 heartbeat/단순 TYPE 잡음은 없었다.
  이번 입력량에서 로그 유실 알림·assert/fatal은 관측하지 않았다.

## 최초 실패와 수정

첫 INFO 실행은 기존 즉시 출력 기준으로 timeout 로그를 너무 일찍 판정해 실패했다.
종료 시 수집된 원시 로그에는 timeout이 한 번 기록돼 있었다.
[실패 결과](test-results/logging-2026-11-14/normal-initial-timing-failure/results.json),
[당시 콘솔](test-results/logging-2026-11-14/normal-initial-timing-failure/console.log),
[최초 스크립트](test-results/logging-2026-11-14/uart-check-initial.py)를 보존한다.
시험 스크립트에 최대 2초의 deferred 콘솔 출력 대기를 추가한 뒤 재플래시·재시험했다.
펌웨어 timeout 값을 바꾸지 않았다. 이 결과로 엄격한 검출/출력 지연 상한을 입증하지 않는다.

## 한계와 종료 상태

- 강제 로그 버퍼 포화·드롭 알림 발생, 반복 경고 폭주, 최악 부하·스택 여유·장시간은 시험하지 않았다.
- 강제 RX overflow, UART 하드웨어 오류, TX 큐 포화·실제 동시성 전수 검증은 제외했다.
- 줄 접두사·내용 확인은 이번 관측 범위의 증거이며 모든 경쟁 조건에서의 무손실 출력을 보장하지 않는다.
- INFO에서는 상태 전이를 통해 측정 처리를 확인했다. 개별 보정 9회는 DEBUG 로그로 확인했다.
- GUI 화면은 이번에 재검증하지 않았다. UART STATUS 파서·클라이언트와 실제 콘솔을 사용했다.
- 시험 후 일반 INFO 펌웨어로 복원하고 STATUS CLI 응답을 확인했다. 포트는 닫았다.

복원 증거: [OpenOCD 로그](test-results/logging-2026-11-14/restore-flash.log),
[STATUS 일곱 통계 0 응답](test-results/logging-2026-11-14/restored-status.log).
저장된 RX 바이트를 재파싱하여 일반·디버그 각각 21개 응답의 TYPE=6·payload 28바이트·CRC를 확인했다.
[일반 wire 검사](test-results/logging-2026-11-14/normal/wire-check.json),
[디버그 wire 검사](test-results/logging-2026-11-14/debug/wire-check.json).
