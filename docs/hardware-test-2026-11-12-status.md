# 2026-11-12 STATUS 실보드 왕복 검증

대상 revision: `2aa57d9` (시험 시작 시 clean). STM32F746G-DISCO / Zephyr 4.2.0.
펌웨어 HEX SHA-256: `2200003281125846f9797218a0bd9c125c1aa305038f0161bb6e608a3a51b9a7`.
데이터 `/dev/cu.usbserial-A5069RR4`, 콘솔 `/dev/cu.usbmodem1203`, 115200 8N1.

## 빌드·플래시

`west build -d <repo>/build -b stm32f746g_disco <repo>/fcc_app -- -DCMAKE_EXPORT_COMPILE_COMMANDS=ON`
및 `west flash -d <repo>/build --runner openocd` 성공.
기존 devicetree duplicate unit-address 경고 유지.
OpenOCD 초기 CPU 식별 경고 후 Cortex-M7 인식·32768바이트 쓰기·실행 완료.
이후 실제 UART 응답으로 펌웨어 실행을 확인했다. 펌웨어 소스 변경 없음.

## 결과

| 시험 | 결과 |
| --- | --- |
| 실제 status_client CLI | 네 통계 0 응답, 종료 코드 0 |
| 반복 STATUS 3회 | 동일 통계 응답 |
| 첫 측정 전 INIT 후 1.2초 대기 | 무수신 timeout 없음 |
| CRC 1비트 변조 | crc_errors +1, 정상 STATUS 복귀 |
| LEN 0·33 입력 | length_errors +2 |
| 부분 측정 7바이트 후 1.3초 중단 | frame_timeouts +1 |
| RESET·INIT 후 재조회 | 오류 통계 보존 |
| 측정 10개 + 중간 STATUS 3회 | 초기화 1회·측정 수용 9회, 측정 중 timeout 없음 |
| 측정 중지 후 STATUS 지속 | 측정 무수신 timeout 1회, 재검사 시 중복 없음 |
| 마지막 통계 | CRC 1, 길이 2, 부분 프레임 timeout 1, 큐 드롭 0 |
| 실제 GUI STATUS | 동일 통계·조회 성공 시각 표시 |
| GUI 측정 중/송신 중지 후 조회 | 모두 성공, 측정 진행 확인 |
| GUI 종료 | worker 종료·포트 정리 완료 |

## 증거

- [조건별 결과·HEX 해시](test-results/status-2aa57d9/results.json)
- [실제 USART6 TX/RX 바이트](test-results/status-2aa57d9/traffic.json)
- [ST-Link 콘솔](test-results/status-2aa57d9/console.log)
- [GUI 결과](test-results/status-2aa57d9/gui-results.json)
- [GUI 실제 화면](test-results/status-2aa57d9/gui-status.png)

GUI는 실제 PySide6 창과 실제 포트를 사용하며 버튼 호출을 자동화했다. 그래프는 PC 합성값이다.

## 요구사항과 한계

FUT-002 기본 STATUS 왕복을 확인했다. REQ-PRO-002, RX-003, STA-001/002,
CTL-001/002, TX-001 및 GUI 수동 조회의 보드 증거를 추가한다.
실제 RX 카운터가 INIT/RESET 이후 유지되는 것도 이 경로에서 관측했다.
16개 조건 확인은 정식 host 테스트 개수와 별개이며 전체 Python/C 테스트를 이번에 재실행하지 않았다.

링버퍼 강제 overflow/drain, UART overrun·framing/parity, 실제 TX 큐 포화,
케이블 분리 중 복구, 최대 부하·장시간·정량 지연·모든 동시성은 검증하지 않았다.
요청 ID가 없으므로 지연된 과거 응답을 엄격히 구분하는 기능은 없다.
