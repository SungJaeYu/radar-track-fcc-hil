# 2026-11-22 로그 형식·GUI/PC·STM32 회귀

`2e87523` 이후 로그 형식 변경 작업본을 STM32F746G-DISCO에서 검증했다.
이 문서와 함께 커밋한 소스가 대상이다. [원문·결과·소스 해시](test-results/log-format-2026-11-22/manifest.json)를 보존했다.
시험 종료 후 일반 이미지를 복원하고 STATUS의 모든 오류 카운터가 0임을 확인했다.

## 변경 범위

애플리케이션과 bench의 로그 형식 문자열 50개를 `항목: key=value`로 맞췄다.
필드는 공백으로 구분하고 단위는 키에 포함한다. Zephyr 자체 접두사·부팅 banner·로그 유실 안내는 프레임워크 출력이므로 변경하지 않았다.
PERF header는 `PERF summary:`, 무표본은 `status=no_samples`, 스택 단위는 `_bytes`로 표시한다.
파서는 과거 로그를 읽으며 기존 JSON 키 `over20`, `unused`, `min_unused`를 보존한다.
펌웨어 이진 프레임, PC simulator의 프로토콜, GUI의 상태·좌표 해석 및 CSV 스키마는 바뀌지 않았다.

## Host·빌드

- Python 전체: **783 passed, 9 subtests passed**. Qt GUI·worker·PC 송신/파싱·기록·평가 테스트를 포함하며 skip 없이 실행했다.
- 영향 범위 CTest **128개**: RX 45, handler 일반 31/PERF 32, TX 일반 4/PERF 4, collector/reporter 12.
- 일반·debug·PERF·log_stress·transport_stress의 **5개 펌웨어 빌드** 성공.
- 기존 devicetree duplicate unit-address 경고는 남는다.

## 실보드 결과

데이터 포트는 FT232R `/dev/cu.usbserial-A5069RR4`, 콘솔은 ST-Link `/dev/cu.usbmodem1203`이며 115200 baud를 사용했다. 포트는 각 시험이 단독 소유했다.

| 시험 | 결과 | 증거 |
|---|---|---|
| PERF 50Hz·10초 smoke | 27개 판정 통과. 부하 측정 500건, 전체 유효 503건, STATUS 11/11, TRACK 508건. PC RTT p99 8.213ms·최대18.554ms, 보드 RX→제출 최대1ms | [결과](test-results/log-format-2026-11-22/perf/results.json) |
| DEBUG traffic | 목표10/50/100/200Hz·복귀 단계 11개 판정 통과. 로그 유실2861개 관측 후 정상 측정·timeout 로그 복귀, STATUS 오류0 | [결과](test-results/log-format-2026-11-22/debug/results.json) |
| 합성 로그 producer | 13개 판정 통과. 20,000개 시도, 로그 유실19879개 관측, 종료 표식과 정상 처리 복귀 확인 | [결과](test-results/log-format-2026-11-22/producer/results.json) |
| RX/handler/TX 정지·포화 | 95개 판정 통과. 새 BENCH 표식, 링 overflow·큐 거부 진단, 복구 후 일반 이미지/STATUS0 확인 | [결과](test-results/log-format-2026-11-22/transport/results.json) |
| Qt GUI 실제 UART | offscreen Qt MainWindow에서 측정29건·TRACK30건·STATUS1건, 보드 추정 표시와 새 콘솔 표시, CSV 저장 확인 | [결과](test-results/log-format-2026-11-22/gui/result.json) · [화면](test-results/log-format-2026-11-22/gui/gui.png) |

DEBUG traffic의 목표 Hz는 실제 송신률 보장이 아니다. STATUS 질의·호스트 대기가 포함되며 각 단계의 실제 송신 건수는 결과 JSON에 남겼다. 로그 유실은 이 스트레스 시험의 의도한 조건이며 데이터 UART 손실과 구분한다.

PERF 무입력 요약에서 predict/correct `n=0`을 확인했다. 마지막 요약은 각각501개 표본이었다.

| 연산 | 최소 ns | 평균 ns | 최대 ns |
|---|---:|---:|---:|
| predict | 27018 | 29175 | 87578 |
| correct | 33361 | 35388 | 94694 |

평균은 cycle 정수 나눗셈 후 변환한 값이다. 인터럽트·선점·조회 비용이 포함될 수 있으며 ns 표시는 ns 해상도나 WCET 보장이 아니다. 이번 결과는 짧은 회귀이며 새 30분 지속 시험을 수행하지 않았다.

## 발견·수정 이력

- 새 일반 로그 형식으로 바뀐 뒤 기존 host 테스트의 문장형 문자열 기대값을 수정했다.
- 첫 DEBUG 실보드 실행은 실제 통신/STATUS 검사가 통과했지만 시험기의 구형 부팅·전이·timeout 문자열 때문에 3개 판정이 실패했다. [최초 결과](test-results/log-format-2026-11-22/debug-initial/results.json)를 보존하고 시험기를 수정한 뒤 다시 실행하여 11/11 통과했다.
- 최초 샌드박스 실행은 장치 접근 권한으로 보드 시험을 시작하지 못했다. 장치 접근 허용 후 실제 시험을 실행했으며 실패를 실보드 성능 결과로 취급하지 않았다.
- GUI는 실제 Qt 위젯과 UART를 사용하는 offscreen 자동 시험이다. 사용자가 직접 데스크톱 창을 조작한 수동 시험은 아니다.

## 재현

[PERF](../fcc_app/tests/test_perf_monitor/README.md), [로그 스트레스](../fcc_app/tests/log_stress/README.md), [전송 포화](../fcc_app/tests/transport_stress/README.md)의 명령을 따른다. PERF는 `--duration 10 --smoke`, DEBUG는 `--mode traffic --build-dir build_debug`, producer는 `--mode producer --build-dir build_log_stress`로 실행했다. 전송 포화는 기본 STATUS 모드이며 `--track-controls` 추가 모드는 이번에 재실행하지 않았다.

[GUI 실행 스크립트](test-results/log-format-2026-11-22/gui_check.py)는 당시 경로·포트가 명시된 증거용이다. 재실행 시 경로와 새 출력 디렉터리를 조정한다. `.gz` 파일은 원문을 무손실 압축했으며 해시는 압축 전 파일 기준이다. 과거 11월18~19일 검증 원문은 수정하지 않았다.
