# 2026-11-14 overflow Kconfig 실보드 회귀

대상 revision: `1e956c5`. STM32F746G-DISCO, Zephyr 4.2.0.
데이터 `/dev/cu.usbserial-A5069RR4`, 콘솔 `/dev/cu.usbmodem1203`, 115200 8N1.

## 방법과 결과

`west flash -d <repo>/build --runner openocd`로 DRAIN 펌웨어를 올린 뒤,
`build_discard`의 DISCARD 펌웨어에도 동일한 UART 회귀 시험을 수행했다.
각 정책에서 16개 조건이 모두 통과했다.

- 첫 측정 전 INIT 후 1.2초 동안 측정 timeout 없음.
- 반복 STATUS 응답과 통계 일관성.
- CRC 변조 1회, LEN 0·33, 부분 프레임 7바이트 후 1.3초 중단에 대해 각각 통계 +1/+2/+1.
- INIT·RESET 후 오류 통계 보존 및 정상 STATUS 응답 복귀.
- 측정 10개 전송 시 초기화 1회·정상 갱신 9회, 중간 STATUS 조회 3회 성공.
- 측정 중 timeout 없음. 측정 중지 후 STATUS만 보내면 측정 timeout이 한 번 발생.
- 마지막 통계: CRC 1, 길이 2, 부분 프레임 timeout 1, 큐 드롭 0.

시험 종료 시 기본 DRAIN 펌웨어를 다시 플래시했다. 복원 후 실제 STATUS CLI가
종료 코드 0과 네 통계 모두 0을 반환한 것을 확인했다.
최초 OpenOCD CPU 식별 경고 이후 Cortex-M7 인식·플래시 쓰기에 성공했으며,
실제 UART 응답으로 실행을 확인했다.

## 원시 증거

- [DRAIN 결과와 HEX SHA-256](test-results/overflow-kconfig-1e956c5/drain/results.json)
- [DRAIN UART 바이트](test-results/overflow-kconfig-1e956c5/drain/traffic.json)
- [DRAIN 콘솔](test-results/overflow-kconfig-1e956c5/drain/console.log)
- [DISCARD 결과와 HEX SHA-256](test-results/overflow-kconfig-1e956c5/discard/results.json)
- [DISCARD UART 바이트](test-results/overflow-kconfig-1e956c5/discard/traffic.json)
- [DISCARD 콘솔](test-results/overflow-kconfig-1e956c5/discard/console.log)

## 검증 범위

두 Kconfig 선택으로 만든 실제 펌웨어의 UART·STATUS·오류 처리 회귀를 검증했다.
링버퍼 overflow를 강제로 발생시키거나 정책별 폐기/보존 차이를 실보드에서 관측한 시험은 아니다.
그 정책별 검증은 host RX 39개 시험에 의존한다. 실제 IRQ 경쟁·UART overrun·부하·장시간 시험과 GUI 재검증은 이번 범위에 포함하지 않았다.
