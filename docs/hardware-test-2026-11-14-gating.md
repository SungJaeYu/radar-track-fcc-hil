# 2026-11-14 게이트 Kconfig 실보드 시험

기준 펌웨어: `610e01e`. STM32F746G-DISCO / Zephyr 4.2.0.
ST-Link `0667FF485153826687125047`, FT232R `A5069RR4`.
콘솔 `/dev/cu.usbmodem1203`, USART6 `/dev/cu.usbserial-A5069RR4`, 115200 8N1.

## 결과

| 구성 | 거리/각도 half-width | 빌드·플래시 | UART 회귀 | 게이트 사례 |
|---|---|---|---|---|
| 변경값 wide | 250 m / 30° | 성공 | 16/16 통과 | 14/14 통과 |
| 기본값 normal | 100 m / 10° | 성공 | 16/16 통과 | 14/14 통과 |

변경값 시험 후 기본 펌웨어로 플래시하여 기본값 시험까지 완료했다.
보드는 **100 m·10°, DRAIN, 두 timeout 1000 ms, CONFIG_ASSERT 비활성** 상태로 남겼다.
시험의 오류 주입으로 통계가 증가했으며 초기화하지 않았다. 재부팅하면 통계는 초기화된다.

### UART 회귀 — 구성별 16개 조건

STATUS 반복 왕복, CRC 오류 +1, 잘못된 LEN +2, 부분 프레임 timeout +1,
INIT/RESET 후 누적 통계 보존을 확인했다. 정상 측정 10개에서 초기화 1회·수용 9회,
추적 중 STATUS 조회 3회, 수신 중 무수신 timeout 없음, 첫 측정 전 감시 없음,
측정 중지 후 STATUS만 송신해도 timeout 1회, 이후 STATUS 정상 응답을 확인했다.

### 게이팅 — 구성별 14개 사례

매 사례마다 INIT → 최초 측정(1000 m, 초기 각도) → 100 ms 뒤 두 번째 측정을 보낸다.
초기 속도 0인 새 트랙에서 검사하므로 이전 사례의 보정·속도 추정 영향이 없다.
콘솔에서 초기화 1회와 예상 수용/거부 1회를 확인한다.

- 거리: 양·음 정확한 경계는 수용, 경계보다 1 m 초과하면 거부 (4개).
- 각도: 양·음 경계의 0.1° 안쪽은 수용, 0.1° 바깥쪽은 거부 (4개).
- 두 게이트 모두 안쪽이면 수용, 모두 바깥쪽이면 거부 (2개).
- 179° → -179°, -179° → 179°는 최단 각도 차이를 사용하여 수용 (2개).
- 동일 입력 비교: 거리 오차 150 m와 각도 오차 20°는 기본값에서 각각 거부,
  변경값에서는 각각 수용 (2개). 실제 설정 적용 여부를 교차 확인했다.

게이트 시험 전후 CRC/LEN/timeout/queue drop 통계는 같았다.
거리 또는 각도만 초과하는 사례도 포함하므로 두 조건의 AND 동작을 확인했다.
콘솔 원문을 보존하고 판정할 때만 기존 `main alive` heartbeat 문자열을 제거했다.

## 원시 증거 및 재현

| 구성 | UART 결과 | UART 콘솔 / wire | 게이트 결과 | 게이트 콘솔 / 송신 | 설정 |
|---|---|---|---|---|---|
| 기본값 | [결과](test-results/gating-2026-11-14/normal/results.json) | [콘솔](test-results/gating-2026-11-14/normal/console.log) / [wire](test-results/gating-2026-11-14/normal/traffic.json) | [결과](test-results/gating-2026-11-14/normal/gate-results.json) | [콘솔](test-results/gating-2026-11-14/normal/gate-console.log) / [송신](test-results/gating-2026-11-14/normal/gate-traffic.json) | [설정](test-results/gating-2026-11-14/normal/config.txt) |
| 변경값 | [결과](test-results/gating-2026-11-14/wide/results.json) | [콘솔](test-results/gating-2026-11-14/wide/console.log) / [wire](test-results/gating-2026-11-14/wide/traffic.json) | [결과](test-results/gating-2026-11-14/wide/gate-results.json) | [콘솔](test-results/gating-2026-11-14/wide/gate-console.log) / [송신](test-results/gating-2026-11-14/wide/gate-traffic.json) | [설정](test-results/gating-2026-11-14/wide/config.txt) |

UART 결과에는 소스 파일별 SHA-256, 전체 `.config` SHA-256 및 HEX SHA-256을 기록했다.
게이트 결과의 HEX SHA-256과 UART 결과의 값도 일치한다.
실행에 사용한 [UART 스크립트](test-results/gating-2026-11-14/uart-check.py)와
[게이트 스크립트](test-results/gating-2026-11-14/gate-check.py)를 당시 경로·포트 그대로 보관한다.
이 스크립트들은 pytest 자동 테스트가 아니며 실장치에 오류 프레임도 송신한다.
재현 시 해당 펌웨어를 먼저 플래시하고 모니터를 종료한 뒤, 각 스크립트에 `wide` 또는
`normal` 인자를 전달한다. 다른 환경에서는 절대경로와 포트를 맞춰야 한다.

## 검증 범위의 한계

각도는 semicircle wire 변환으로 양자화되므로 보드에서는 ±0.1° 여유를 둔다.
정확한 float 경계·최소/최대 설정은 앞선 host 행렬 시험 결과이며 이번 보드 시험과 구분한다.
콘솔의 소수 둘째 자리 출력으로 정밀 필터 오차를 검증한 것은 아니다.
디버그 빌드, GUI, 강제 링 overflow, UART 하드웨어 오류, 부하·장시간 시험은 이번 범위에 포함하지 않았다.
