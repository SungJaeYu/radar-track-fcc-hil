# 검증 기록

[문서 목차](README.md) · [테스트 목록](test-cases.md) · [V1 완료 범위](roadmap.md#v1-acceptance)

날짜별 기록은 당시 소스·설정에 대한 증거입니다. 아래 표에서 필요한 결과로 이동할 수 있습니다. 과거의 미검증 상태나 테스트 개수를 현재 상태로 해석하지 않습니다.

| 확인할 내용 | 결과 |
|---|---|
| 최신 잡음·이상치·RTT 회귀 | [149회 추적 평가](hardware-test-2026-11-19-noise-spike.md) |
| 30분 송수신·지연·스택 | [성능 관측과 당시 판정](hardware-test-2026-11-19-performance.md) |
| 정상·잡음·누락·GUI 왕복 | [고정 시나리오 평가](hardware-test-2026-11-18-evaluation.md) |
| 오류·포화·제어 후 복귀 | [전송 오류](hardware-test-2026-11-18-transport-stress.md) · [제어·TRACK](hardware-test-2026-11-18-track-controls.md) |

이하 내용은 검증 및 판정 변경 이력입니다. 최신 사용법은 각 도구의 안내를 따릅니다.

## 2026-11-22 — estimator 실행 시간 집계·출력

`495d70d` 이후 작업본에서 predict/correct cycle 측정·집계와 누적 최소/평균/최대 ns 로그를 연결했다.
collector/reporter CTest 12개, PERF handler 32개, PERF TX 4개(합계 48개),
latency guard 및 콘솔 파서 Python 25개가 통과했다. 일반·debug·PERF 펌웨어 빌드가 통과했다.
PERF 구성 시 기존 devicetree의 중복 unit-address 경고가 출력되었으나 빌드는 성공했다.
flash·실보드 HIL은 수행하지 않았으며 기존 30분 기록을 새 실행 시간 출력의 증거로 사용하지 않는다.
[계측 계약](performance-monitoring.md)과 [재현 명령](../fcc_app/tests/test_perf_monitor/README.md)을 따른다.

## 2026-11-19 — pull 후 RTT·이상치49회·잡음100회 실보드 검증

`eb15fb1` 기준 Python765개+subtest9개, 변경 영향을 받는 CTest84개 등록 항목(논리 사례104회) 통과. 일반/perf 빌드 최신성 확인 후 짧은 RTT 회귀27개 판정 통과: p99 10.745ms·최대25.378ms, 정상 이미지 복원·STATUS0.

일반 펌웨어의 기본 이상치49회 모두 통과, 잡음100회 모두 기록·응답 검사 완료. 총149세션의 측정17,880개·TRACK18,327개·STATUS298개가 원시 wire와 대응했고 STATUS 통계는 모두0이었다. 이상치 복귀 시작 최대3.1초는 센서 시간축 관측이다. 거리 잡음30m의 seed별 평균 RMSE는 측정30.875m→추정11.434m, 각도 잡음3°는3.043°→1.193°였다. CSV 재평가와 원시 프레임 재검증·archive 바이트 동일성을 확인했다. [전체 조건·지표·한계·재현](hardware-test-2026-11-19-noise-spike.md).

펌웨어·제품 코드는 수정하지 않았고 일반 이미지·최종 STATUS0·포트 해제를 확인했다. 이번 실행은30분 부하 재시험이나 dropout100회가 아니며, V1 완료 상태·과거 판정은 유지한다.

## 2026-11-19 — 순간 이상치 보드 실행기·회복 판정

정상 추적 중 측정만 변경하는 `spike_sweep`와49회 기본 행렬(대조군1, 거리/각도24값 × 단발/5연속)을 추가했습니다. wire 범위 밖 입력은 실행 전에 거부합니다. 첫 게이트 판정, 최대 추정 오차, 무효/누락, 정상 복귀 후3연속 오차/상태 회복과 센서 시간축 회복 지연을 기록합니다. 재획득 warm-up을 회복 평가에서 제외하지 않습니다.

Python765개+subtest9개(Qt offscreen 포함), C host166회(CTest146개) 통과. 새 C handler48조합은 일반/perf 두 구성에서 실제 필터·보고 코드로 확인했습니다. 펌웨어 소스·튜닝은 변경하지 않았으며 새 STM32 빌드·플래시·49회 실보드 실행은 하지 않았습니다. [실행·판정·결과 파일](../pc_sim/README.md#순간-이상치정상-복귀-실보드-시험).

## 2026-11-19 — 잡음 행렬·입력 경계 보강

평가 CLI에 거리/각도 잡음·seed 선택과 한 축씩 변화시키는100회 기본 행렬을 추가했습니다. RMSE·최대 절대 오차·평가 표본 비율·게이트 거부·응답 대응·트랙 상태 집계를 보존하며 일부 실행 실패를 성공 평균에 섞지 않습니다. 입력 설정/생성 표본을 UART 접근 전에 검증합니다. 펌웨어 소스·필터 튜닝은 변경하지 않았습니다.

Python703개+subtest9개(Qt offscreen 포함), C host164회(CTest144개: tracking 내부21함수 포함) 통과. 측정 wire 경계, 디코더 길이/출력 보존, 칼만 dt/Q/R·비유한 입력, PC 설정·CLI·warm-up·순번 경계를 보강했습니다. 상세는 [입력 경계 점검](test-cases.md#입력-경계범위-점검--2026-11-19)과 [실행법](../pc_sim/README.md#잡음-크기seed-반복-평가)을 따릅니다. 이 실행은 host/모사 UART 검증이며 STM32 빌드·플래시·새 잡음 행렬 실보드 실행은 수행하지 않았습니다. 기존V1 실측 수치는 변경하지 않습니다.

## 2026-11-19 — V1 완료·문서 정리

프로젝트 소유자의 V1 완료 확인을 반영했습니다. README는 핵심 기능·30분 송수신 통계·실행 안내로 축약하고 상세 구조는 EXPLAINER, 설정은 보드 안내에 모았습니다. [완료 범위·근거·후속 과제](roadmap.md#v1-acceptance)를 기준으로 합니다. 최신 저장 기록은 측정89,990건 모두 대응, PC RTT p99 8.049ms·최대34.894ms로 현재 예산 이내입니다. 아래 과거 실패 판정은 당시 조건의 이력이며 보존합니다. 이번 작업은 문서 정리이고 새 실보드 실행·추가 성능 개선 측정은 없습니다.

## 2026-11-19 — PC 왕복 지연 중심으로 판정 변경

사용자 요청으로 호스트 송신 간격100ms·평균률±1%의 자동 실패 조건을 제거했다. 두 값은 실제 부하의 참고 통계로 보존한다. PC RTT 분포·최대를 주 지표로 보고한다. 후속 사용자 지적을 반영해 기본 예산 p99 ≤20ms·최대 ≤100ms를 적용하며 실행 전 변경값을 기록한다. 응답/오류/순번/보드20ms/스택 검사는 유지한다. [현재 계약](performance-monitoring.md)을 따른다. 아래 과거 실행의 실패 판정과 원시 증거는 당시 기준이며 변경하지 않았다. 이번 변경은 실보드 재실행이나 성능 개선 달성 기록이 아니다. 성능 파서·RTT 판정11개 및 기록·STATUS·TRACK 관련63개 Python 테스트가 통과했다.

## PC 진행 집계 수정·성능 재시험 — 2026-11-19

`7caf593`으로 짧은 보드 회귀26개를 통과하고 30분 부하를 완료했습니다. Python239개·subtest9개, 일반 빌드 확인·perf 재빌드도 통과했습니다. 펌웨어 소스는 변경하지 않았습니다.

측정89,990개 모두 응답, STATUS1,801개 통계0, 보드 RX→TX 제출 최대1ms, 큐 거부0, 스택 여유784/1,472/736B였습니다. 진행 출력 직후 간격은 모든 분 경계에서 약20ms로 유지되어 누적 순회 병목 해소를 확인했습니다. 그러나 별도 공백137.581ms가100ms 기준을 넘었으므로 **전체 시험 실패·V1-4 미완료**입니다. 정상 이미지 복원·STATUS0과 wire/CSV 독립 재검증을 완료했습니다. [조건·실패 지점·한계·원시 기록](hardware-test-2026-11-19-performance.md).

## 성능 계측 실보드·30분 관측 — 2026-11-18

펌웨어 `25c8bb9`의 perf 이미지를 사용했습니다. 짧은 시험26개 판정 통과, 첫 장시간 실행은 USB 제거 기록과 함께 중단됐고 일반 이미지로 복원했습니다. 재시험은1,800초 동안 측정89,971개를 모두 응답했고 STATUS1,801개가 모두0, 보드 지연 최대1ms·큐 드롭0·관측 스택 여유784/1,472/736B였습니다.

호스트 write 간격 최대163.995ms가 사전 기준100ms를 초과하여 **전체 부하 시험은 실패**로 유지합니다. 순번·wire/CSV 재검증은 별도 통과했으며 일반 이미지 복원·STATUS0을 확인했습니다. [중단 이력·판정·그래프·압축 원시 기록](hardware-test-2026-11-18-performance.md). 펌웨어는 변경하지 않았습니다. 증거 파서 Python8개를 추가하여 전체239개+subtest9개 통과. C/펌웨어는 이전 계측 커밋과 동일합니다.

## 선택적 성능 계측 — 2026-11-18

`7618d3c` 이후 작업본에 `perf.conf`, collector/reporter, handler/TX 계측 호출을 추가했습니다. 지연 집계·큐 관측값·TX polling 완료·스택 조회를 제공하며 일반/debug 구성에서는 제외합니다. [측정 계약·실행 절차](performance-monitoring.md).

- Python 231개 + subtest 9개 통과.
- C host 160회 실행 통과: 기존 116회 + 계측 활성화 44회(monitor/reporter 9·handler 31·TX 4). CTest 등록 수는 144개이며 Tracking의 기본 17개 함수를 한 등록에서 실행합니다.
- 시간 경계·실패/미제출 분리·문맥 해제·포화·동시 snapshot·백분위 구간·스택 조회 오류 복귀를 확인했습니다. pthread mutex와 모사 uptime/stack API이며 RTOS 타이밍 증거가 아닙니다.
- STM32F746G-DISCO 일반·debug·perf 빌드 모두 통과. 최종 빌드에서 컴파일 경고는 없었습니다. 일반/debug 최종 설정에는 RADAR_PERF_MONITOR가 꺼져 있고 perf만 켜져 있습니다.

| 구성 | FLASH bytes | RAM bytes |
|---|---:|---:|
| 일반 | 42,784 | 15,696 |
| debug.conf | 58,784 | 16,976 |
| perf.conf | 44,604 | 16,464 |

perf의 일반 대비 증가는 FLASH 1,820바이트·RAM 768바이트입니다. 링커 영역 사용량이며 실행 중 스택 여유 실측이 아닙니다. 이 변경에서는 flash·실보드 시험·30분 부하 시험을 수행하지 않았습니다. 다음은 계측 이미지의 짧은 보드 검증 후 선언 부하 지속 시험입니다.

## 오래된 측정·CSV·실보드 평가 — 2026-11-18

`da6ffe5` 이후 작업본에서 stale 큐 측정 폐기·포화 카운터, STATUS32바이트, GUI CSV·평가 CLI를 추가했습니다.
Python231개+subtest9, C116회와 일반/디버그 빌드 통과. 일반 이미지를 플래시해 정상·잡음·연속 누락 시나리오 및 실제 UART offscreen GUI13개 확인 항목을 통과했습니다.
거리 잡음 시나리오 RMSE는 측정7.601m→추정2.739m였습니다. 세 실행 모두 관측 순번 공백0입니다. 각도 고정·짧은 시험이며 지속 부하·보드 내부 지연/스택 검증은 남았습니다.
[대상 소스·실행 조건·CSV·실패 이력·캡처·한계](hardware-test-2026-11-18-evaluation.md).

## TRACK 실보드 왕복 — 2026-11-18

`e66515e` 일반 빌드·flash 후 실제 USART6에서 64개 확인 항목 통과, TRACK 29개 수신(순번 0~28). INIT/RESET·timeout 1회 보고, 예측 결과·상태 전이, 오류 통계·정상 복귀·STATUS 혼합 수신을 확인했습니다. GUI·부하·정량 성능은 이번 범위가 아닙니다. [절차·결과·원시 증거](hardware-test-2026-11-18-track.md).

## Timeout·예측 결과 분리 — 2026-11-18

- INIT/RESET·무수신 timeout의 입력 없는 RESET 보고를 연결하고 순번 소모·중복 방지를 host로 검증했습니다.
- processor timeout 판정은 const 입력으로 상태를 보존하고 handler가 초기화·로그·보고를 담당합니다. RX timeout도 내부 판정과 공개 처리를 분리했습니다.
- `track_predict()`는 내부 `TrackPredictResult`로 예측 성공·EMPTY·시각 거부·초기화를 구분합니다. 기존 추적 정책·wire enum 값은 유지합니다.
- C host 115회(RX 45, handler 29, TX 4, Tracking 17+게이트 20), Python 221개 + subtest 9개 통과. 문서 링크 592개 검사 정상.
- 일반 펌웨어 빌드 성공: FLASH 42,656 / RAM 15,696바이트. 이번에는 디버그 빌드·flash·실보드 시험을 수행하지 않았습니다.

## 최신 코드와 검증 경계 — PC TRACK·GUI, 2026-11-17

- 기준: `cf63e15` 이후 PC/문서 변경. 펌웨어 소스는 수정하지 않았습니다.
- Python **221개 + subtest 9개** 통과. 기존 172개에 TRACK 디코더 35·C 인코더 상호운용 5·표시 모델 3·worker/Qt 6개를 추가했습니다.
- STATUS 조회 시작/timeout이 부분 TRACK 프레임을 지우지 않도록 수정하고, 혼합 수신·잘못된 TRACK 제외 후 정상 복귀를 확인했습니다.
- GUI의 보드 추정값·상태/순번·유효성·2초 STALE, 모사 UART 송신/수신과 재시작을 확인했습니다. 1280×850·1000×650 offscreen 캡처로 배치도 검토했습니다.
- README에 시스템 입출력과 펌웨어 ISR/링버퍼/스레드/큐의 Mermaid 다이어그램 두 개를 추가했습니다.
- 이번 범위에서 C host suite 재실행·펌웨어 빌드·flash·실보드 시험은 하지 않았습니다. C 인코더 상호운용 host 컴파일은 Python 시험에 포함됩니다.
- INIT/RESET·무수신 timeout 이벤트 보고, 원시 결과 저장·정량 평가, TRACK 경로의 실제 보드 왕복은 남아 있습니다.

## 이름·경로 정리와 로그 판정 — 2026-11-17

- 기준: 이름·경로 정리 `fd96c17`, 로그 부하 판정 수정 `838aca6`. [변경 전후](naming-review.md).
- 변경 전 Python 160개 + subtest 9개, C host 115회 실행 통과. 이름 변경 후 같은 회귀도 통과했습니다.
- 최종 Python **172개 + subtest 9개** 통과. 추가된 12개는 로그 시험기의 혼합 STATUS/TRACK 수용과 오류 거부를 검사합니다. 수정 전 혼합 입력 두 사례의 실패를 재현했습니다.
- C host **115회** 통과: RX drain 24/discard 21, handler 29, TX 4, Tracking 17, 게이트 설정 행렬 20. CTest 등록은 Tracking 묶음 때문에 99항목입니다.
- 일반·디버그 펌웨어 빌드 성공. FLASH/RAM은 일반 42,608/15,696바이트, 디버그 58,608/16,976바이트입니다. 기존 devicetree 중복 주소 및 디버그 assert 활성화 안내는 유지됩니다.
- 일반/디버그와 네 host suite의 compile database에서 파일 존재·옛 소스 경로 제거를 확인했습니다. `test_tracking` 이동으로 해당 CMake cache를 새로 구성했고 로컬 VS Code의 기존 `build_tracking/compile_commands.json` 연결을 유지했습니다. VS Code 화면 진단 자체는 재확인하지 않았습니다.
- TRACK_REPORT 추가본의 flash·실보드 왕복·PC payload 디코더·GUI/정량 평가는 남아 있습니다. 아래 실보드 기록은 모두 그 이전 버전입니다.
- 로그 부하 실행기는 STATUS/TRACK의 CRC·길이를 구분하지만 TRACK 수치·순번·전달률은 검사하지 않습니다. 수정 후 실보드 재시험은 남아 있습니다. [도구 제한](../fcc_app/tests/log_stress/README.md#대상-버전과-현재-제한).

재현 명령: `python -m pytest -q`; 각 host README의 CMake/CTest 명령;
[일반 빌드](board-bringup.md#1-빌드플래시)와 [디버그 빌드](logging.md#빌드와-확인).
하드웨어 포트나 flash 없이 수행한 검증입니다.

## 실보드 시험 기록

| 기록 | 주요 범위 |
|---|---|
| [로그 포화·측정 부하](hardware-test-2026-11-14-log-stress.md) | 생성기 20,000개·최고 평균 157Hz, 유실 알림·STATUS 122회·복귀, 기본 INFO 복원 |
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

## 소비 스레드 정지 기반 포화 검증 — 2026-11-18

기본 DRAIN 실보드에서 강제 링 overflow·RX 큐 포화/stale 폐기·STATUS TX 큐 포화와 각 단계 정상 추적 복귀를 확인했습니다. 일반 이미지로 복원했습니다. [결과·실패 이력·제한](hardware-test-2026-11-18-transport-stress.md). TRACK TX 포화·제어 유실/순서·지속 부하/자원 시험은 남았습니다.

## TRACK 포화·제어 순서 후속 검증 — 2026-11-18

실보드112개 확인 항목 통과: RX 큐 포화 시 INIT/RESET 유실, 접수된 제어/측정 FIFO 순서, TRACK TX 포화 후 순번 공백과 정상 추적 복귀를 확인했습니다. 일반 이미지 복원 완료. [결과·원시 증거·계약](hardware-test-2026-11-18-track-controls.md). 제어 우선 보존 정책은 추가하지 않았으며 다음은30분 부하·MCU 내부 지연·큐/스택 계측입니다.
