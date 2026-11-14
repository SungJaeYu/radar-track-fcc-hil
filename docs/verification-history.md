# 검증 기록

프로젝트의 현재 기능과 다음 작업은 [README](../README.md), 시험 사례·개수는
[테스트 목록](test-cases.md), 요구사항별 검증 범위는 [추적표](requirements-traceability.md)를 참고합니다.
이 문서는 검증 증거의 진입점입니다. 날짜별 기록은 당시 소스·설정에 대한 결과이며,
이전의 미검증 상태나 테스트 개수를 현재 상태로 해석하지 않습니다.

## 실보드 시험 기록

| 기록 | 주요 범위 |
|---|---|
| [로깅 일반·디버그](hardware-test-2026-11-14-logging.md) | 각 30조건, 레벨·측정·상태 전이·WARN 문장, STATUS/오류/timeout; 강제 포화 제외 |
| [확장 STATUS 28바이트](hardware-test-2026-11-14-status-extended.md) | UART 18조건, 통계 7개, CLI·GUI 측정 병행 조회; 강제 overflow 제외 |
| [RX/TX 물리 단절·복원](hardware-test-2026-11-14-link-loss.md) | 두 방향 데이터 선 단절, 무수신/응답 timeout, 재부팅 없는 복귀 |
| [게이트 설정](hardware-test-2026-11-14-gating.md) | 기본/변경 게이트 값의 수용·거부, UART 회귀, 기본 펌웨어 복원 |
| [각도 정규화·수치 보호](hardware-test-2026-11-14-fmodf.md) | 일반/디버그 UART 회귀, ±179° 경계 왕복 |
| [assert](hardware-test-2026-11-14-assert.md) | 일반/디버그 빌드와 정상 입력 실보드 시험 |
| [overflow 정책 Kconfig](hardware-test-2026-11-14-kconfig.md) | DRAIN/DISCARD 펌웨어 UART 회귀; 강제 overflow 시험은 제외 |
| [STATUS CLI·GUI](hardware-test-2026-11-12-status.md) | STATUS 왕복, 오류 통계, 추적 중 조회·무수신 timeout |
| [수신 모듈 분리·GUI](hardware-test-2026-11-01.md) | 측정·중지·재시작·오류 후 복귀·큐 burst |

## 기록 관리

- README에는 현재 검증 상태와 이 문서의 링크만 유지합니다.
- 새 시험은 소스 버전·설정·판정·원시 로그·미검증 범위를 상세 결과 문서에 기록합니다.
- 새 결과 문서를 위 목록에 연결하고, 테스트 목록과 요구사항 추적표를 필요에 따라 갱신합니다.
- host 테스트, 펌웨어 빌드, flash, 실보드 실행을 구분합니다. 설정별 UART 회귀 통과를
  강제 overflow나 부하 검증으로 확대해서 해석하지 않습니다.

## README에서 옮긴 검증 기록

아래는 2026-11-14 정리 시점의 원문 기록입니다. “최신”이라는 표현도 당시 시점을 뜻합니다.

2026-11-12 GUI 실보드: `2aa57d9` 빌드·플래시 후 STATUS CLI·GUI 왕복, 오류 통계 증가,
INIT/RESET 통계 보존, 추적 중 조회·무수신 timeout 확인 완료.
[실보드 결과와 원시 로그](hardware-test-2026-11-12-status.md).

최신 host: Python **136개 + subtest 9개**, Tracking **17개**·handler **17개** 통과.
RX drain 21·discard 18·TX 4·게이팅 설정 20개를 합한 현재 C 사례 수는 97회 실행입니다.
게이팅 기본값(100 m/10°)과 변경값(250 m/30°) 펌웨어 빌드도 통과했습니다.
게이트 기본값·변경값 실보드 시험도 구성별 UART 16개·게이트 14개 통과했습니다.
[결과와 원시 로그](hardware-test-2026-11-14-gating.md). 보드는 기본값으로 복원했습니다.
일반·디버그 펌웨어 빌드와 보드 UART 회귀 및 ±179° 경계 왕복은
[2026-11-14 fmodf 실보드 결과](hardware-test-2026-11-14-fmodf.md)를 참고합니다.
GUI 실보드 검증은 11월 12일 결과이며 최신 GUI 자동 검증은 offscreen 모사 시험입니다.
강제 링 overflow·하드웨어 UART 오류·부하·보드 assert 강제 실패는 미검증입니다.
아래는 버전별 과거 실행 기록이며 당시 미검증·실패 상태를 그대로 보존합니다.

2026-11-12 (`0e84bdd`): PC 프레임 파서·STATUS CLI 포함 Python 126개 + subtest 9개 통과.
최신 펌웨어 빌드 성공; 기존 devicetree 중복 unit-address 경고 유지.
ST-Link·USB-UART가 감지되지 않아 flash는 OpenOCD `Error: open failed`로 실패했으며,
STATUS 실보드 왕복은 미검증입니다. 아래 날짜별 기록은 당시 버전의 결과입니다.

2026-11-10: assert 제외 코딩 규칙 적용 완료. RX 21개·STATUS 5개·TX 4개·Tracking suite, Python 84개 + subtest 9개, 공개 헤더 단독 컴파일과 펌웨어 빌드 통과.

2026-11-10: enum·TYPE 정리 후 Python 84개 + subtest 9개, RX/STATUS/TX/Tracking host 테스트와 펌웨어 빌드 통과. STATUS_REPORT는 예약 정의이며 실보드 재시험은 하지 않았습니다.

2026-11-08: RX 20개·STATUS 5개·TX 4개·CRC/인코더 상호운용 26개 통과.
이번 펌웨어 증분 빌드는 경고 없이 통과했습니다. TX 실보드 시험은 아직입니다.

2026-11-04: UART host 20개와 STATUS handler host 5개 통과, 펌웨어 빌드 통과.
CMake 재구성 시 기존 보드 devicetree의 `0x90000000` 중복 unit-address 경고가 남아 있습니다.
통계·STATUS 변경의 flash/실보드 시험은 수행하지 않았습니다.

2026-11-03: 부분 프레임 timeout 회귀 6개를 추가해 UART host C는 19개입니다.
이 기능의 실보드 검증은 아직 남아 있습니다.

2026-11-01 분리본 기준 Python 74개 + subtest 9개, UART host C 13개를 통과했고
펌웨어 빌드·플래시와 실제 GUI 연결·송신·중지·재시작을 확인했습니다.
CRC·길이 오류 후 복귀, timeout, 큐 포화 후 정상 복귀도 보드에서 확인했습니다.
**링버퍼 overflow는 해당 시험에서 발생하지 않았으므로 실보드 drain 검증은 미완료입니다.**
USART6 TX, 장시간·최대 부하와 폐루프 추적 정확도도 아직 검증하지 않았습니다.

[실보드 시험 기록](hardware-test-2026-11-01.md) · [테스트 목록](test-cases.md).
자동 테스트·빌드 성공과 실제 보드 검증은 구분합니다. 과거 구현은 Git 이력에서 확인할 수 있습니다.


### 추가 이전 기록

2026-11-14 assert 검증: 공통 host assert 어댑터를 연결하고 내부 전제 위반 3개 사례를 추가했습니다.
[일반·디버그 실보드 검증](hardware-test-2026-11-14-assert.md)을 참고합니다.

## 모듈별 로깅 전환 (2026-11-14)

[설정·레벨·검증 경계](logging.md). C host 103회, Python 160개 + subtest 9개와
일반/디버그 펌웨어 빌드 검증. host는 로그 텍스트를 동기 캡처하며 실제 로깅 스레드·포화를 모사하지 않는다.
이번 전환의 flash·실보드 출력·부하 검증은 수행하지 않았다.
