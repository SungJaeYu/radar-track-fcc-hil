# TRACK TX 포화·INIT/RESET 유실 및 순서 — 2026-11-18

기준 `f42cc50` 이후 시험 실행기에 `--track-controls`만 추가한 작업본입니다. 제품 펌웨어와4초 정지 fixture는 변경하지 않았습니다. STM32F746G-DISCO, USART6 115200/8N1, 기본 DRAIN·timeout1000ms·INFO입니다.
[소스·설정·HEX 식별](test-results/track-controls-2026-11-18/metadata.json).

## 방법과 결과

[기존 포화 시험 도구](../fcc_app/tests/transport_stress/README.md)의 명령에 `--track-controls`를 추가합니다. 별도 시험 이미지를 flash하고 RX·handler·TX 소비 스레드 정지 중 실제 UART로 입력합니다. IRQ 수신은 유지합니다. 종료 시 일반 이미지를 다시 flash하고 STATUS0을 확인합니다.

**112개 assertion 확인 항목 통과**입니다. write 확인을 포함한 개수이며 독립 시나리오112개가 아닙니다.

| 단계 | 확인 결과 |
|---|---|
| RX 정지 | 기존 회귀: 링 overflow1, ISR 폐기345바이트, 정상 추적 복귀 |
| handler 정지·측정24개 후 INIT/RESET | 큐드롭18 = 측정16 + 제어2; 접수된8개 측정은 stale 폐기, backlog TRACK 및 제어 보고 없음 |
| 정상 구간의 측정→RESET→측정→INIT→측정 | 순번5~9, STARTED→입력 없는 RESET→STARTED→입력 없는 RESET→STARTED; 새 트랙 거리1000/1100/1200m 확인 |
| TX 정지·측정24개 | 정지 중 TRACK 없음; 재개 후 순번15~23의9개가 입력 시각0~800ms 순서대로 수신됨 |
| TX 포화 후 INIT | 다음 보고 순번40, 공백16개; TRACK queue full 경고16회와 일치 |
| 최종 복귀 | 새 INIT·측정으로 CONFIRMED, RESET; 일반 이미지 복원 후 여덟 통계0 |

TX 큐8개 외에 워커의 대기/처리 중 항목1개가 있을 수 있습니다. 이번에는9개가 보존됐습니다. 측정 보고24개 중15개와, 입력 중단 후 발생한 무수신 timeout 보고1개가 포화로 거부됐습니다. 총16개의 실패한 제출 시도도 순번을 소모하므로23→40의 간격과 일치합니다.

마지막 시험 STATUS는 CRC0/LEN0/부분timeout0/큐드롭18/링overflow1/ISR폐기345/복구폐기0/stale8입니다. PC 수신 CRC/LEN 오류도 없었습니다. 순번 공백은 이 통제된 시험에서 콘솔·입력 개수로 원인을 대응한 것이며 일반 운용에서 공백만 보고 유실 위치를 확정할 수는 없습니다.

## 현재 제어 계약

INIT/RESET도 같은 FIFO와 새 요청 거부 정책을 사용하므로 RX 큐가 차면 버려질 수 있습니다. 이번 시험은 이 동작을 확인했으며 제어 우선순위·전용 큐·ACK를 추가하지 않았습니다.
접수된 요청은 FIFO 순서로 처리하며 reset은 큐를 비우지 않습니다. 따라서 reset 뒤에 접수된 새 측정은 새 트랙을 만들 수 있습니다. 복구 시 트래픽을 낮추고 초기화를 다시 보낸 뒤 새 측정의 STARTED/CONFIRMED를 확인해야 합니다. UART write 성공은 처리 성공 확인이 아닙니다.

wire에 제어 요청 ID나 세션 ID가 없어 지연된 RESET 보고의 원인을 항상 구분하지 못합니다. 이 시험은 순서를 통제하고 다른 원인을 배제한 구간에서 판정합니다.

## 증거·검증 범위

- [판정](test-results/track-controls-2026-11-18/results.json), [콘솔](test-results/track-controls-2026-11-18/console.log), [원시 TX/RX](test-results/track-controls-2026-11-18/wire.json).
- [TRACK 필드](test-results/track-controls-2026-11-18/reports.json), [STATUS](test-results/track-controls-2026-11-18/status.json).
- [시험 flash](test-results/track-controls-2026-11-18/bench-flash.log), [일반 복원 flash](test-results/track-controls-2026-11-18/restore-flash.log).
- Python231개+subtest9 재실행 통과, 실행기 문법 검사 통과. 펌웨어 소스·이미지 변경이 없어 기존 host116회·일반/시험 이미지 빌드 증거를 재사용했습니다. 이번에는 두 이미지를 실제 flash했습니다.
- 소비 스레드 정지는 인위적인 고장 주입입니다. 최대 처리율·30분 안정성·MCU 내부 지연·스택·모든 IRQ 경쟁 조건을 증명하지 않습니다. DISCARD/물리 단선/USB 제거는 이번에 수행하지 않았습니다.
- 기존 STATUS TX 포화 모드는 유지합니다. 이 실행에서는 TRACK TX 모드만 재시험했습니다. UI 조작 시험은 포함하지 않았습니다.
