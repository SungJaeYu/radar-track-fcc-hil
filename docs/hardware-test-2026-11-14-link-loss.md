# RX/TX 물리 단절·복원 시험 — 2026-11-14

STM32F746G-DISCO, USART6 PC7(RX)/PC6(TX), FT232R USB-UART, 115200 8N1.
사용자가 데이터 선을 직접 분리·복원하고 자동 스크립트가 송수신 및 콘솔을 판정했다.
전원·ST-Link·GND는 유지했고 시험 중 보드를 재부팅하거나 플래시하지 않았다.
저장소 기준은 `10f2775`; 이전 게이트 시험에서 기본값 100 m/10°·DRAIN 펌웨어로
복원한 보드를 사용했다. 이번에는 flash 메모리를 읽어 이미지 해시를 재검증하지 않았다.

## 결과

| 단계 | 조작 | 판정 |
|---|---|---|
| `rx_disconnected` | 어댑터 TX → PC7 분리 | 보드 콘솔에 STATUS 처리 없음, PC는 1초 제한의 응답 timeout |
| `rx_restored` | 위 선 복원 | STATUS 응답, INIT 후 추적 초기화 1회·갱신 4회 |
| `tracking_cut` | 정상 연속 송신 도중 위 선 분리 | 정상 추적 후 무수신 timeout·트랙 정리 1회 |
| `tx_disconnected` | RX 선 복원, PC6 → 어댑터 RX 분리 | 보드 콘솔에는 STATUS 처리 있음, PC에는 응답 없이 timeout |
| `tx_restored` | 두 데이터 선 모두 복원 | STATUS 응답, INIT 후 추적 초기화 1회·갱신 4회 |

5단계·14개 판정 조건 모두 통과했다. STATUS timeout의 관측 허용 시간은 0.95–2초였다.
단절 시각은 수동 조작이므로 실제 단선부터 timeout까지의 정밀 지연은 측정하지 않았다.
추적 단절 시험은 송신을 계속하다 timeout을 관측한 뒤 추가 1.3초 동안 중복 로그 여부를 확인했다.
최종 상태는 데이터 선 모두 연결, PC 포트 닫힘이다. 측정 송신 종료 후 무수신 timeout은 정상 정책이다.

## 증거

각 단계의 파일 접두사는 위 표의 단계명과 같다.

| 단계 | 판정 | 콘솔 원문 | PC wire 기록 |
|---|---|---|---|
| `rx_disconnected` | [JSON](test-results/link-loss-2026-11-14/rx_disconnected-results.json) | [로그](test-results/link-loss-2026-11-14/rx_disconnected-console.log) | [TX/RX](test-results/link-loss-2026-11-14/rx_disconnected-traffic.json) |
| `rx_restored` | [JSON](test-results/link-loss-2026-11-14/rx_restored-results.json) | [로그](test-results/link-loss-2026-11-14/rx_restored-console.log) | [TX/RX](test-results/link-loss-2026-11-14/rx_restored-traffic.json) |
| `tracking_cut` | [JSON](test-results/link-loss-2026-11-14/tracking_cut-results.json) | [로그](test-results/link-loss-2026-11-14/tracking_cut-console.log) | [TX/RX](test-results/link-loss-2026-11-14/tracking_cut-traffic.json) |
| `tx_disconnected` | [JSON](test-results/link-loss-2026-11-14/tx_disconnected-results.json) | [로그](test-results/link-loss-2026-11-14/tx_disconnected-console.log) | [TX/RX](test-results/link-loss-2026-11-14/tx_disconnected-traffic.json) |
| `tx_restored` | [JSON](test-results/link-loss-2026-11-14/tx_restored-results.json) | [로그](test-results/link-loss-2026-11-14/tx_restored-console.log) | [TX/RX](test-results/link-loss-2026-11-14/tx_restored-traffic.json) |

[실행 스크립트](test-results/link-loss-2026-11-14/runner.py)는 당시 절대경로·포트를 보존했다.
위 단계명을 인자로 전달하며, 단계마다 표에 지정한 배선 상태를 먼저 맞춘다.
`tracking_cut`은 `CUT_NOW` 출력 후 RX 선을 분리한다. 최대 120초 동안 송신한다.
콘솔 원문은 보존하고 판정에서는 알려진 `main alive` heartbeat만 제외했다.
단절 단계 JSON의 `request_reached_handler`는 실제 도착 여부 자체가 아니라
예상값과 일치했다는 판정이다. RX 단절에서는 미도착, TX 단절에서는 도착이 기대값이다.

## 범위와 한계

실제 데이터 선 단절과 재연결은 검증했다. 보드가 단선 원인을 진단했다는 뜻은 아니다.
RX는 정상 측정 무수신으로, PC는 STATUS 응답 deadline으로 통신 불능을 감지한다.
`uart_poll_out()`에는 상대 수신 확인이 없으므로 TX 단선 오류 카운터 증가는 요구하지 않았다.
STATUS는 요청 ID가 없으며 재전송·ACK·중복 제거는 이번 검증 대상이 아니다.
USB 어댑터 제거·전원 재인가·오배선·고부하·UART overrun 및 반복 내구 시험은 수행하지 않았다.
