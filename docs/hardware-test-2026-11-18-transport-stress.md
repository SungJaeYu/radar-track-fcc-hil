# 링·RX/TX 큐 강제 포화 시험 — 2026-11-18

기준은 `794b10b`에 시험용 Kconfig/CMake·fixture만 추가한 작업본입니다. 제품 RX/handler/TX 로직은 변경하지 않았습니다. STM32F746G-DISCO, 기본 DRAIN·timeout1000ms·INFO, USART6 115200/8N1입니다.

[시험 도구와 명령](../fcc_app/tests/transport_stress/README.md)은 기본 비활성인 `CONFIG_RADAR_TRANSPORT_STRESS_TEST`를 사용합니다. 시험 스레드가 IRQ를 유지하며 RX·handler·TX를 차례로4초 정지시키고 각 단계 사이4초 복구 구간을 둡니다.

## 결과

95개 assertion 확인 항목 통과입니다. 개별 write 완료 확인을 포함하므로 독립 시나리오95개라는 뜻은 아닙니다.

| 단계 | 주입 | 관측 |
|---|---|---|
| RX 정지 | 600바이트의 0 | 링 overflow1회, 255바이트 소비, ISR 폐기345바이트, recovery_discarded_bytes0 |
| handler 정지 | 10ms 간격 측정24개 | RX 큐 드롭16, 나이 초과 측정 폐기8, backlog TRACK 없음, 추가 링 overflow 없음 |
| TX 정지 | STATUS24개 | 정지 중 응답0, 재개 후 응답9개, queue full WARN15회 |
| 단계별 복구 | INIT·정상 측정3개·RESET | 매 단계 CONFIRMED 복귀 및 정상 STATUS |
| 최종 복원 | 일반 펌웨어 flash | STATUS 여덟 통계0, 포트 종료 |

TX 큐 깊이는8입니다. `k_msgq_get()` 대기 중 직접 전달되거나 이미 워커에 전달된 항목1개가 추가로 있을 수 있습니다. 실제 이번 실행에서는9개가 보존됐습니다. 수신 CRC/LEN 오류는 없었습니다. 최종 시험 통계는 CRC0/LEN0/부분timeout0/큐드롭16/링overflow1/ISR폐기345/복구폐기0/stale8입니다.

## 최초 실패와 재시험

최초2초 정지 시험에서 deferred 로그가 PC에 약1.1초 늦게 도착했습니다. 마커를 받은 뒤 보낸 측정은 handler 재개 시 나이1초 미만이라 정상 처리됐고 stale 증가 assertion이 실패했습니다. 실제 큐드롭16은 관측됐습니다. 이는 나이 초과 시험 조건이 성립하지 않은 것이며 펌웨어 기준을 바꾸지 않았습니다.
[실패 판정](test-results/transport-stress-2026-11-18/results.json), [콘솔](test-results/transport-stress-2026-11-18/console.log), [당시2초 fixture](test-results/transport-stress-2026-11-18/initial-fixture.c)를 보존했습니다. 실패 후에도 일반 이미지 복원·STATUS0 확인은 성공했습니다. 정지를4초로 늘리고 새 결과 디렉터리로 재실행했습니다.

## 증거와 한계

- [성공 판정·이미지 해시](test-results/transport-stress-2026-11-18-retry/results.json), [소스·설정 식별](test-results/transport-stress-2026-11-18-retry/metadata.json).
- [콘솔](test-results/transport-stress-2026-11-18-retry/console.log), [TX/RX 원시 바이트](test-results/transport-stress-2026-11-18-retry/wire.json), [TRACK](test-results/transport-stress-2026-11-18-retry/reports.json), [STATUS](test-results/transport-stress-2026-11-18-retry/status.json).
- [시험 flash](test-results/transport-stress-2026-11-18-retry/bench-flash.log), [일반 복원 flash](test-results/transport-stress-2026-11-18-retry/restore-flash.log).
- 실제 IRQ·링·큐·UART를 사용했지만 고장 주입은 인위적인 thread suspension입니다. 정상 부하의 자연 포화·최대 처리율·IRQ 경쟁 전수·UART 하드웨어 overrun 검증이 아닙니다.
- 링에 넣은 데이터는0 잡음이므로 overflow 중 완성 가능한 정상 프레임 보존/부분 프레임 경계의 실보드 행렬까지 검증하지 않았습니다. 이 부분은 기존 host 증거를 유지합니다.
- TX는 STATUS로 채웠습니다. TRACK 보고 순번 누락을 포함한 TX 포화 검증, INIT/RESET 포화 유실·순서, DISCARD 실보드 강제 포화는 남았습니다.
- 시험 앱 FLASH43,480 / RAM16,912바이트. 일반 이미지는 FLASH42,784 / RAM15,696바이트이며 일반 구성에는 fixture가 제외됩니다.
- Python231개+subtest9 재실행 통과. 제품 C 변경이 없어 host C는 앞선116회 결과를 재사용했습니다. 30분 부하·MCU 내부 지연·스택 실측은 수행하지 않았습니다.
