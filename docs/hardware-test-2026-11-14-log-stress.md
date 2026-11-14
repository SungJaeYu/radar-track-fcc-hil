# 로그 포화·측정 부하 실보드 시험 — 2026-11-14

기준 소스 `0b284c6` + 이 커밋의 시험 fixture. STM32F746G-DISCO, USART6 데이터와
ST-Link 콘솔 각각 115200 baud. 기본 DRAIN, 로그 버퍼 4096바이트, deferred/nonblocking/drop-new.
소스·설정·HEX 식별자는 [metadata](test-results/log-stress-2026-11-14/metadata.json)에 기록했습니다.

## 결과

| 시험 | 측정 송신 | 로그 폐기 알림 합계 | 정상 STATUS | 최대 PC 호출 경과 |
|---|---:|---:|---:|---:|
| 20,000개 시험 로그 + 약 10Hz 측정 | 179개 | 19,860개 | 65회 | 60.70ms |
| 일반 DEBUG 측정 부하 | 906개 | 1,909개 | 57회 | 56.47ms |

두 시험 모두 RX 통계 7개가 처음부터 끝까지 0, wire CRC/길이 오류 0,
단일 부팅·CONFIRMED·마지막 측정 로그·송신 중단 뒤 1회 timeout을 확인했습니다.
강제 생성기는 시작·완료 로그까지 확인했습니다. 생성기 sequence 161개를 수집했으며
폐기 알림은 애플리케이션 로그도 포함하므로 `20000 - 161`과 일치할 필요가 없습니다.

측정 부하 각 구간은 3초입니다. 목표 10/50/100/200/10Hz에 대해
실측 평균은 **10.00/42.66/82.00/157.32/10.00Hz**였습니다.
STATUS를 같은 루프에서 조회하므로 송신 공백이 생깁니다. 200Hz 지속 송신 보장이 아닙니다.
PC STATUS 호출 경과에는 최대 50ms read 대기·USB/OS 지연이 포함되므로 보드 처리 지연으로 해석하지 않습니다.

## 증거와 실행 이력

- [생성기 판정](test-results/log-stress-2026-11-14/producer-retry/results.json), [콘솔](test-results/log-stress-2026-11-14/producer-retry/console.log), [wire](test-results/log-stress-2026-11-14/producer-retry/traffic.json), [wire 재검사](test-results/log-stress-2026-11-14/producer-retry/wire-check.json)
- [측정 부하 판정](test-results/log-stress-2026-11-14/traffic/results.json), [콘솔](test-results/log-stress-2026-11-14/traffic/console.log), [wire](test-results/log-stress-2026-11-14/traffic/traffic.json), [wire 재검사](test-results/log-stress-2026-11-14/traffic/wire-check.json)
- 최초 producer 실행은 west workspace 미지정으로 플래시 전에 실패했습니다. [실패 기록](test-results/log-stress-2026-11-14/producer/flash.log)을 보존하고 `--zephyr-workspace` 추가 후 재실행했습니다. 펌웨어 실패가 아닙니다.
- 후속 wire 전체 검사 함수는 저장된 두 캡처를 재생해 검증했습니다. hardware 재실행과 구분합니다.
- 일반 INFO/일반 DEBUG/시험 이미지 빌드 통과. 기본·일반 DEBUG 이미지에는 시험 소스가 포함되지 않음을 확인했습니다.
- Python 160개 + subtest 9개, RX 45개·handler 17개·TX 4개·tracking 17개와 게이트 조합 20개 통과.
- [기본 INFO 복원 flash](test-results/log-stress-2026-11-14/restore-flash.log), [복원 후 STATUS](test-results/log-stress-2026-11-14/restored-status.log): 7개 모두 0.

## 결론과 남은 범위

이번 부하에서는 로그가 유실되어도 STATUS 통신과 측정 후 정상 복귀가 유지됐습니다.
RX 통계 0은 하드웨어 overrun 부재나 모든 측정 처리 완료를 증명하지 않습니다.
반복 WARN 제한, IRQ 경쟁, 최대 지속 처리율, TX 큐 포화, 스택·CPU 측정, 장시간 시험,
전체 측정의 추정 결과 검증은 남습니다. GUI 재시험은 수행하지 않았습니다.
재현 방법은 [시험 안내](../fcc_app/tests/log_stress/README.md)를 참고합니다.
