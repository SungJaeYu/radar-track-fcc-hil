# 2026-11-14 assert 일반·디버그 보드 회귀

STM32F746G-DISCO / Zephyr 4.2.0, USART6와 ST-Link 콘솔 115200 8N1.
기준 커밋 `1eb7440` 이후 assert 변경본을 시험했다. 정확한 시험 소스·설정·HEX 해시는 각 results.json에 기록했다.
펌웨어 입력 처리 로직의 assert와 정적 제약은 사용자 작성이며, host 어댑터·검증 코드는 별도로 추가했다.

## 구성

- 일반: `build`, CONFIG_ASSERT 비활성, DRAIN, 두 timeout 1000ms.
- 디버그: `/private/tmp/radar-assert-review-debug`, prj.conf에 debug.conf를 EXTRA_CONF_FILE로 병합.
- 디버그에서는 CONFIG_ASSERT=y, CONFIG_ASSERT_LEVEL=2이며 정상 펌웨어와 같은 수신 설정을 사용한다.
- 디버그·일반 순서로 플래시해 각각 검증하고, 보드에는 일반 펌웨어를 남긴다.

## 검증

각 이미지에서 아래 16개 조건을 확인한다: 첫 측정 전 timeout 없음, 반복 STATUS 유지,
CRC +1, LEN +2, 부분 프레임 timeout +1, RESET/INIT 통계 보존,
추적 중 STATUS 3회, 트랙 초기화 1회·갱신 9회, 측정 중 timeout 없음,
STATUS만 수신할 때 측정 timeout 발생, timeout 1회 처리, 최종 STATUS 정상.

host C는 RX 39·handler 17·TX 4·tracking 함수 13, 총 73회 실행을 통과했다.
Python 133개와 subtest 9개를 통과했다. 추가된 3개는 자식 프로세스에서 NULL 링,
잘못된 parser 상태, 무한대 process noise에 대해 SIGABRT와 메시지를 확인한다.
host 어댑터는 NDEBUG와 무관하게 활성화되며 Zephyr 펌웨어 include 경로에는 들어가지 않는다.

## 증거와 한계

- [디버그 결과](test-results/assert-2026-11-14/debug/results.json), [UART](test-results/assert-2026-11-14/debug/traffic.json), [콘솔](test-results/assert-2026-11-14/debug/console.log)
- [일반 결과](test-results/assert-2026-11-14/normal/results.json), [UART](test-results/assert-2026-11-14/normal/traffic.json), [콘솔](test-results/assert-2026-11-14/normal/console.log)

이 보드 시험은 정상 입력·통신 오류가 내부 assert를 잘못 유발하지 않는지 확인한다.
보드 내부 상태를 의도적으로 손상시켜 fatal/assert 실패 경로를 발생시키지는 않았다.
강제 링 overflow, UART 하드웨어 오류, 부하·장시간 시험과 GUI 재시험은 범위 밖이다.
기존 OpenOCD 초기 식별 경고 이후 Cortex-M7 인식과 플래시 쓰기에 성공했다.

일반 빌드의 한 실행에서 `Measurement amain alive\r\nccepted`처럼 heartbeat가
측정 로그 중간에 삽입되어 문자열 계수 검사가 실패했다.
[실패 원시 로그](test-results/assert-2026-11-14/normal-log-interleaving/console.log)와
[실패 판정](test-results/assert-2026-11-14/normal-log-interleaving/results.json)을 보존했다.
재시험에서는 판정용 문자열에서 알려진 `main alive` heartbeat만 제거하고 원시 로그는 그대로 저장했다.
스레드 간 콘솔 출력의 원자성은 보장되지 않으며, 로그 직렬화는 별도 후속 작업이다.
