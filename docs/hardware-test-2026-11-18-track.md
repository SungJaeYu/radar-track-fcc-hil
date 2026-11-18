# TRACK·초기화·timeout 실보드 시험 — 2026-11-18

대상은 `e66515e`의 일반 펌웨어입니다. STM32F746G-DISCO에 빌드·플래시한 뒤 USB-UART로 실제 응답을 수집했습니다. 제품 코드는 수정하지 않았습니다.

## 환경과 실행

- 데이터: `/dev/cu.usbserial-A5069RR4`, USART6, 115200/8N1.
- 진단 콘솔: `/dev/cu.usbmodem1203`, ST-Link VCP.
- DRAIN 정책, 부분 프레임/측정 timeout 각각 1000ms, 거리/방위 게이트 100m/10°.
- 일반 빌드 성공: FLASH 42,656 / RAM 15,696바이트.
- `west flash -d build --runner openocd` 성공. 최초 CPU examination에서 `PARTNO 0x0` 경고/오류가 있었으나 이어서 Cortex-M7을 인식하고 flash 쓰기를 완료했으며 종료 코드는 0입니다. 아래 UART 결과로 프로그램 실행을 별도로 확인했습니다.
- 이미지 `zephyr.hex` SHA-256: `04afb9920bcf278d0f357bf0e186fe8b4e941219852ff74e59bdf3c91c92c0c1`.

저장소 루트에서 실행합니다. 스크립트의 포트 이름을 환경에 맞추고 다른 프로그램의 포트 점유를 해제해야 합니다. INIT/RESET을 보내므로 진행 중인 추적 상태를 초기화합니다.

```sh
PYTHONPATH=. python docs/test-results/track-2026-11-18/uart-check.py
```

## 결과

**64개 assertion 확인 항목 통과**, 시험 약 11.85초. TRACK 29개(순번 0~28)를 수신했으며 관측 순번 공백과 PC 수신 CRC/LEN 오류는 없었습니다. 이 개수는 독립 시나리오 64개가 아니라 단일 시험 흐름의 확인 항목 수입니다.

| 항목 | 확인 결과 |
| --- | --- |
| INIT/RESET | EMPTY·RESET, 입력/추정 플래그 0, timestamp/추정값 0인 보고를 한 번 제출 |
| 최초 측정 전 | 1.2초 대기 동안 timeout 보고 없음 |
| 정상 추적 | timestamp 0의 STARTED·유효 플래그, 1000m/0rad 디코딩, 3회 측정 후 CONFIRMED |
| 중복·역행 시각 | TIME_REJECTED, 이전 추정 시각 유지 |
| 게이트 거부·복귀 | COASTING, 정상 측정으로 CONFIRMED 복귀 |
| 잘못된 측정 | 거리 0·9바이트 payload에 TRACK 없음, 무수신 감시를 갱신하지 않음 |
| 측정 timeout | 무효 RESET 보고 한 번, 이후 1.2초 추가 대기에도 반복 없음, 새 측정으로 재시작 |
| 긴 센서 시간 간격 | INPUT_PRESENT=1인 RESET, 다음 측정에서 STARTED |
| LOST | 5회 gate miss 후 LOST·무효 추정, 다음 측정에서 RESET, 이후 재시작 |
| 수신 오류·복구 | CRC·LEN·부분 프레임 timeout 통계 각각 +1, 이후 정상 TRACK 수신 |
| STATUS 병행 | 같은 수신 스트림에서 TRACK·STATUS 순서와 내용 확인 |
| 관측 손실 | RX 큐·링 overflow·폐기 통계 증가 없음, TRACK 순번 연속 |

콘솔에서도 timeout 로그는 한 번이며, INIT/RESET·CONFIRMED/COASTING/LOST 전이와 재시작을 확인했습니다. 시험 종료 시 RESET을 보내 트랙을 비우고 감시를 해제했습니다.

## 증거와 한계

- [실행 스크립트](test-results/track-2026-11-18/uart-check.py)
- [판정·TRACK 디코딩 결과](test-results/track-2026-11-18/results.json)
- [송수신 원시 바이트와 PC monotonic 경과 시간](test-results/track-2026-11-18/wire.json)
- [ST-Link 콘솔](test-results/track-2026-11-18/console.log)

이번 시험은 일반 펌웨어의 실제 UART와 PC 공통 디코더를 검증했습니다. GUI 화면/worker, 디버그 빌드, 강제 큐·링 포화, 물리 단선, 장시간 부하, 지연 상한·RMSE는 시험하지 않았습니다. 1.2초 수신 창은 timeout 보고 존재/중복 확인용이며 정확한 스케줄링 지연 측정값이 아닙니다.
