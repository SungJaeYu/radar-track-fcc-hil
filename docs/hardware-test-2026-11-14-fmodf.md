# 2026-11-14 fmodf·수치 보호 실보드 시험

STM32F746G-DISCO / Zephyr 4.2.0. 기준 커밋 `0537db0` 이후 작업본.
정확한 시험 소스와 설정·HEX SHA-256은 각 results.json에 기록했다.
USART6 `/dev/cu.usbserial-A5069RR4`, 콘솔 `/dev/cu.usbmodem1203`, 115200 8N1.

## 결과

일반·디버그 펌웨어 모두 빌드·플래시 성공, 각 구성에서 UART 회귀 16개 조건과
각도 경계 6개 조건이 통과했다. 보드는 일반 펌웨어(DRAIN, 두 timeout 1000ms)로 남겼다.

- UART: STATUS 반복 응답, CRC +1·길이 +2·부분 timeout +1, INIT/RESET 통계 유지.
- 측정 10개: 초기화 1회·갱신 9회, 중간 STATUS 조회 3회, 수신 중 timeout 없음.
- 첫 측정 전 무수신 감시 없음, 측정 중지 후 STATUS만 보내면 timeout 1회.
- 각도: 179/-179도를 교대로 6개 송신, 초기화 1회·수용 5회, gate 거부 없음.
- 출력 각도 5개가 모두 유한하고 반올림된 ±π 범위 내, 오류 통계 변화·assert 발생 없음.

콘솔은 소수 둘째 자리까지만 출력하므로 수치 경계의 엄밀한 검증은 host 테스트가 담당한다.
heartbeat 중간 삽입을 고려해 판정 문자열에서 알려진 heartbeat만 제거했다. 원시 로그는 보존했다.

## 원시 증거

| 구성 | UART 결과 | 콘솔 | wire 바이트 | 각도 결과 | 각도 콘솔 |
| --- | --- | --- | --- | --- | --- |
| 디버그 | [결과](test-results/fmodf-2026-11-14/debug/results.json) | [콘솔](test-results/fmodf-2026-11-14/debug/console.log) | [바이트](test-results/fmodf-2026-11-14/debug/traffic.json) | [각도](test-results/fmodf-2026-11-14/debug/angle-results.json) | [콘솔](test-results/fmodf-2026-11-14/debug/angle-console.log) |
| 일반 | [결과](test-results/fmodf-2026-11-14/normal/results.json) | [콘솔](test-results/fmodf-2026-11-14/normal/console.log) | [바이트](test-results/fmodf-2026-11-14/normal/traffic.json) | [각도](test-results/fmodf-2026-11-14/normal/angle-results.json) | [콘솔](test-results/fmodf-2026-11-14/normal/angle-console.log) |

큰 유한값·NaN/Inf 각도·비정상 innovation covariance 주입은 host에서만 확인했다.
보드 fatal 경로·강제 링 overflow·UART 하드웨어 오류·부하·장시간·GUI 실보드 재시험은 수행하지 않았다.
로깅 시스템 전환은 TODO이며 이번 펌웨어는 printk를 유지한다.
