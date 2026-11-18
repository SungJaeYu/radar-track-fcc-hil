# 진행 집계 수정 후 성능 재시험 — 2026-11-19

**30분 실행과 보드 응답 검사는 완료했지만 호스트 송신 간격 기준을 초과하여 전체 판정은 실패다.** V1-4는 미완료로 유지한다. 이전 실패 기록은 [11월 18일 시험](hardware-test-2026-11-18-performance.md)에 보존한다.

## 대상과 변경

- 소스 `7caf593` (`ed6b8ba`의 PC 진행 집계 수정 포함). 매분 누적 응답 리스트를 순회하던 코드를 정상 측정 응답 카운터로 바꿨다. 펌웨어 동작은 변경하지 않았다.
- STM32F746G-DISCO, Zephyr 4.2, `perf.conf`, 기본 DRAIN. 일반 이미지를 복원용으로 별도 준비했다. 임시 TX probe 이미지는 이번 시험에 사용하지 않았다.
- 115200 baud, 8N1, 정지 표적 1,000m·15°, 잡음 0. MEAS_REPORT 50Hz, STATUS 1Hz, 지속 1,800초. 호스트 평균 49.5..50.5Hz·최대 측정 write 간격 100ms 이하, 보드 RX 완료→TX 제출 20ms 이하 기준을 그대로 적용했다.
- `caffeinate -i`로 PC 유휴 절전을 방지했다. 시험 시작은 11월 18일 23:52 KST이며 종료는 19일이다. 정확한 UTC 시작·commit·ELF/스크립트 SHA-256은 [metadata](test-results/perf-counter-load-2026-11-19/metadata.json)에 기록했다.
- 일반 빌드 확인, perf 재빌드 성공. perf FLASH 44,604B·RAM 16,464B. 기존 보드 DTS의 QSPI 주소 중복 경고는 동일하다.
- Python 239개·subtest 9개 통과. 문서 이동으로 바뀐 로컬 링크 대상 310개를 확인했다. C/펌웨어 소스는 변경하지 않았으며 이번에 전체 C host 회귀를 재실행하지 않았다.

## 짧은 보드 회귀

[26개 판정](test-results/perf-counter-smoke-2026-11-19/results.json) 모두 통과했다. 10초 부하 500개와 오류/RESET 후 복귀 3개, 정상 측정 총503개가 응답했다. 무효 거리 1개는 `without_submit=1`로 분리했다. STATUS 11개·TRACK 508개·TX 완료519개가 일치했으며 일반 이미지 복원과 STATUS 여덟 통계 0을 확인했다.

## 30분 결과

[실시간 판정](test-results/perf-counter-load-2026-11-19/results.json) · [독립 wire/CSV 재검증](test-results/perf-counter-load-2026-11-19/offline-audit.json) · [362개 계측 요약](test-results/perf-counter-load-2026-11-19/performance.json.gz) · [콘솔 원문](test-results/perf-counter-load-2026-11-19/console.log.gz).

| 항목 | 결과 |
|---|---|
| 실제 측정 송신 / 대응 응답 | 89,990 / 89,990, 시각·순서 일치 |
| 평균 측정 송신률 | 49.9944Hz, 기준 충족 |
| PC 송신 슬롯 건너뜀 | 10개; 보드가 수신한 측정의 드롭과 구분 |
| 최대 측정 write 간격 | **137.581ms**, 100ms 초과1회 → 기준 미달 |
| TRACK / STATUS 응답 | 89,992 / 1,801; TRACK 순번 연속, STATUS 여덟 통계 모두0 |
| RX 큐 접수 | 91,792 = 측정89,990 + STATUS1,801 + INIT1 |
| TX 큐 접수 / polling 완료 / PC 프레임 | 각각91,793 |
| RX→TX 제출 지연 | p50 0ms, p95/p99 1ms, 최대1ms, 20ms 초과0 |
| 큐 대기 / 처리 지연 최대 | 각각1ms |
| 관측 큐 최대 점유 | RX0 / TX2; 직접 전달·표본 시점에 따른 관측값 |
| 관측 미사용 스택 최솟값 | RX784B / handler1,472B / TX736B |
| PC write→TRACK 수신 지연 | p50 6.386ms / p95 6.633ms / p99 8.049ms / 최대34.894ms |
| wire 재파싱 | CRC·LEN 오류0, 남은 부분 프레임0 |
| 계측 요약 | 362개, 독립 재검증에서 연속성 확인 |
| 복원 | 일반 이미지 flash 성공, STATUS 여덟 통계0 |

`results.json`은 호스트 간격 assertion에서 실패하여 뒤쪽 최종 요약 객체를 만들지 않았다. 위 수치는 원시 CSV·wire·계측 기록을 재계산한 `offline-audit.json`에서 확인한다. 독립 재검증의 `passed=true`는 대응·연속성 검사 결과이며 전체 부하 실패를 덮어쓰지 않는다.

## 수정 효과와 남은 지연

[송신 간격 비교](test-results/perf-counter-load-2026-11-19/pacing-comparison.json)는 각 분 경계의 측정(`timestamp_ms`가 60,000의 배수) 다음 write까지 간격을 비교한다.

- 이전: 누적 응답 순회 비용 때문에 후반 진행 출력 직후 간격이 커졌고, 29분 지점에서 70.309ms였다.
- 이번: 1~29분 경계 모두 **19.542~20.449ms**, 29분 지점은 19.794ms였다. 누적 집계로 증가하던 지연은 관측되지 않았다.
- 별도 단발성 공백: 부하 1,710.780초(28분30.780초)에서 다음 측정 write까지 137.581ms. 진행 출력 경계가 아니다.
- 공백 직전 측정은 송신 후 **6.350ms** 만에 이미 응답을 수신했다. 다음 측정은 1,710.918초에 write했다. 이번 공백을 직전 보드 응답 대기로 설명할 수는 없다.

이번 실행에는 PC 구간별 CPU/wall-time 계측을 추가하지 않았다. OS 스케줄링, 시리얼 I/O, 파일 기록, 그 밖의 루프 처리 중 어느 부분에서 멈췄는지는 확정하지 않는다. [이전 짧은 TX probe](test-results/tx-timing-probe-2026-11-18/README.md)의 polling 수치를 이번 장시간 실행의 실측값으로 사용하지 않는다.

다음은 PC 송신 루프 전체와 시리얼·기록·sleep 구간을 함께 계측하여 단발성 공백의 위치를 좁히는 것이다. 고정된 100ms 기준을 완화하거나 이번 시험을 합격으로 바꾸지 않는다. 펌웨어 polling→DMA 변경의 근거로도 삼지 않는다.

## 보존과 재현

실행 명령은 [시험기 안내](../fcc_app/tests/test_perf_monitor/README.md#real-board-check)를 따른다. 이번에는 `--duration 1800`, 새 출력 디렉터리 `docs/test-results/perf-counter-load-2026-11-19`를 사용했다. 일반 이미지 복원은 시험기의 `finally` 경로에서 수행했고, [복원 STATUS](test-results/perf-counter-load-2026-11-19/restored-status.json)로 확인했다.

원시 CSV·콘솔·flash 로그·계측 JSON은 gzip으로 무손실 압축했다. 압축 전후 원문 바이트 일치를 확인하고 [해시 manifest](test-results/perf-counter-load-2026-11-19/archive-manifest.json)를 남겼다. 다음 명령은 보드 접근 없이 기록을 다시 검증한다.

```sh
python docs/test-results/perf-load-2026-11-18-retry/audit_capture.py docs/test-results/perf-counter-load-2026-11-19
```

성능 관측은 이 조건과 실행 범위에 한한다. 물리 마지막 비트 완료·상대 ACK·WCET·모든 오류/부하 조건의 보장을 뜻하지 않는다.
