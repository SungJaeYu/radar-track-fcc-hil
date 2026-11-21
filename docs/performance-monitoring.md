# 보드 성능 계측

`perf.conf`는 V1 지속 부하 시험에 사용한 별도 펌웨어 구성이다. 시간·큐·송신 통계와 스택 여유를 수집하며 main이 5초마다 ST-Link 콘솔에 누적 `PERF` 요약을 출력한다. UART 프로토콜과 STATUS_REPORT는 변경하지 않는다. 일반·debug 빌드에는 계측 코드가 포함되지 않는다.

## 현재 성능 기준 — 2026-11-19 변경

성능 개선의 주 지표는 **PC 측정 write 직전 → 대응 TRACK이 담긴 데이터를 read한 직후의 왕복 지연**이다. 같은 PC monotonic clock으로 p50/p95/p99/최대와 표본 수를 보고한다. USB·UART·보드 처리·호스트 수신 대기가 포함되며, 파싱 완료나 GUI 표시까지의 시간은 아니다. 비교 실행은 같은 시계 경계·부하·baud·기록 설정을 사용한다.

사용자 결정으로 호스트 최대 송신 간격 100ms 및 평균 송신률 ±1%의 자동 실패 조건을 제거했다. 실제 송신률·슬롯 건너뜀·최대 공백은 적용한 부하를 설명하는 참고 통계로 남긴다. 낮은 실제 부하를 50Hz 성능으로 주장하지 않는다. 응답 대응·오류·순번·보드 내부 20ms 예산·스택 여유 검사는 유지한다.

왕복 지연의 기본 합격 조건은 **p99 ≤ 20ms, 관측 최대 ≤ 100ms**다. 50Hz 한 주기(20ms) 안에 99%의 응답을 받고 다섯 주기(100ms) 이상 오래된 응답을 제한하는 프로젝트 초기 예산이다. 외부 표준이나 실제 비행제어 요구사항에서 도출된 값은 아니다. 응답 누락·중복·오류 0 조건도 함께 만족해야 한다. 시험 전에 `--pc-rtt-p99-ms`와 `--pc-rtt-max-ms`로 변경할 수 있으며 적용값은 metadata와 결과에 남는다. 예산을 초과해도 최종 RTT 통계를 보존한다. p50·p95는 개선 추세를 보는 참고값이다.

[최신 30분 기록](hardware-test-2026-11-19-performance.md)의 기준선은 평균49.994Hz, 측정89,990개 모두 응답, PC RTT p50 6.386ms / p95 6.633ms / p99 8.049ms / 최대34.894ms, 보드 RX→TX 제출 최대1ms다. 호스트 공백137.581ms는 보드 성능 실패 근거로 사용하지 않는다. 당시 기준의 원시 실패 판정은 변경하지 않는다. 이번 기준 변경은 새 실보드 실행이나 지연 개선 실측이 아니다.

V1 완료 후 선택적인 최적화는 RTT의 긴 꼬리가 발생한 구간을 계측하고, 수신 처리 전 동기 기록·콘솔 처리 비용과 TX 대기·전송을 구분하는 것이다. 기록 분리·버퍼링 등은 같은 조건의 RTT 비교로 효과를 확인한다. 송신 간격만 줄이는 작업을 v1 선행조건으로 두지 않는다.

## 빌드와 원복

```sh
cd ~/zephyrproject
west build -d <repo>/build_perf -b stm32f746g_disco <repo>/fcc_app -- -DEXTRA_CONF_FILE=perf.conf -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
# 실제 측정할 때만 플래시한다.
west flash -d <repo>/build_perf --runner openocd
# 시험을 마친 뒤 일반 이미지를 다시 빌드하고 복원한다.
west build -d <repo>/build -b stm32f746g_disco <repo>/fcc_app -- -DEXTRA_CONF_FILE=
west flash -d <repo>/build --runner openocd
```

`<repo>`는 저장소의 절대 경로로 바꾼다. `debug.conf`, `log_stress.conf`, `transport_stress.conf`와 섞지 않는다. DEBUG 프레임 로그나 강제 소비 스레드 정지는 측정 부하 자체를 바꾼다.

| 설정 | 의미 |
|---|---|
| `CONFIG_RADAR_PERF_MONITOR=y` | collector·reporter와 호출 지점 활성화. LOG에 의존하며 스택 조회에 필요한 두 설정을 선택한다 |
| `CONFIG_INIT_STACKS=y` | 시작할 때 스택을 패턴으로 채워 사용 흔적을 조회할 수 있게 한다 |
| `CONFIG_THREAD_STACK_INFO=y` | 스레드 스택 범위 정보를 보관한다 |
| `CONFIG_RADAR_LOG_STRESS_TEST=n` | 합성 로그 폭주 제외 |
| `CONFIG_RADAR_TRANSPORT_STRESS_TEST=n` | RX/handler/TX 강제 정지 제외 |

최종 설정은 `build_perf/zephyr/.config`에서 확인한다. 현재 보드 구성의 kernel tick은 10,000Hz지만 `k_uptime_get()`으로 저장하는 계측 값의 단위는 정수 ms다. 1ms 미만 차이를 세밀하게 구분하지 못하며 tick/cycle 기반 처리시간이나 CPU 점유율 측정이 아니다.

## 시간 기준과 대상

```text
RX 파서 완료              handler process 진입           uart_tx_submit 반환
received_uptime_ms (t0)       k_uptime_get() (t1)              k_uptime_get() (t2)
        └──── queue_wait ──────┘
                                └──── processing ──────────┘
        └──────────────── rx_to_submit ───────────────────┘
```

- `queue_wait`: handler가 받은 **모든 MEAS_REPORT**의 `t1-t0`. 오래된/무효 측정도 포함한다. RX 큐에 들어오지 못한 프레임은 제외한다.
- `processing`: 해당 측정의 TRACK이 TX 큐에 **성공적으로 등록된 경우**의 `t2-t1`.
- `rx_to_submit`: 같은 성공 집합의 `t2-t0`. 게이트/시각 거부 결과도 TRACK을 성공 제출했다면 포함한다.
- `t0`는 기존 보드 RX 완료 시각이다. 첫/마지막 바이트 ISR 시각이나 센서 `input_timestamp_ms`가 아니다.
- `t2`는 송신 큐 제출 반환 직후, 제출 결과 로그 전에 기록한다. `uart_tx_submit()` 내부 계측 비용도 포함한다. UART 실제 전송·PC 수신까지의 지연은 아니다.
- 처리 시간에는 선점·대기와 계측 자체의 비용도 포함된다. 순수 필터 실행 시간이나 WCET라고 해석하지 않는다.

C의 시간 통계 타입은 `perf_latency_stats`이며, `sample_count`는 표본 수,
`histogram_counts`는 구간별 빈도, `over_budget_count`는 기준 초과 횟수다.
큐 통계는 `accepted_count`, `rejected_count`, `max_observed_depth`로 저장한다.
기존 로그 필드 이름은 보드 검증 도구와의 호환을 위해 유지한다.

각 시간 통계는 `n`, `total_ms`, `max_ms`, `over20`, 히스토그램을 유지한다. 평균은 `total_ms / n`으로 계산하며 n=0이면 표본이 없다. 부팅 이후 누적값이고 5초 구간별 통계가 아니다.

히스토그램은 0..20ms 각각 한 구간과 **>20ms 구간 21**로 구성된다. 로그의 `p50_bin`, `p95_bin`, `p99_bin`은 nearest-rank 방식의 구간 번호다. 예를 들어 `p99_bin=21 max_ms=40`이면 p99는 **20ms 초과**, 관측 최대는 40ms라는 뜻이며 p99=21ms라는 뜻이 아니다. 초과 구간의 정확한 백분위가 필요하면 후속 시험에서 더 넓거나 세밀한 계측을 추가해야 한다. `over20`은 엄격한 `>20ms`이며 20ms는 포함하지 않는다.

## 카운터·소유권·집계 범위

| 필드/로그 | 의미 |
|---|---|
| `measurements` | handler에 진입한 MEAS_REPORT 수 |
| `without_submit` | 측정 처리 종료까지 TRACK 제출 호출이 없던 수. stale·무효 payload·TRACK 인코딩 실패를 포함하며 세 원인은 기존 로그/STATUS로 구분 |
| `submit_failed` | 측정 TRACK의 `uart_tx_submit()` 실패 수. 성공 시간 분포에서 제외 |
| `invalid_time` | 음수·역행 보드 시각 때문에 시간 표본을 제외한 수. 정상 uptime 흐름에서는 0 기대 |
| `queue rx/tx accepted/rejected` | 유효 submit 인자의 큐 등록 성공/실패. RX는 모든 프레임 TYPE, TX는 STATUS·제어 TRACK까지 포함 |
| `observed_max_used` | 제출 직후 조회한 큐 점유 수의 최대. 소비자가 이미 꺼낼 수 있어 실제 최대 점유율의 하한 관측값 |
| `tx poll_completed` | 프레임의 모든 `uart_poll_out()` 호출을 마친 수. 상대 수신 ACK나 마지막 stop bit 완료가 아님 |
| `tx encode_failed` | TX 워커에서 wire 인코딩에 실패한 수 |
| `stack rx/handler/tx unused/min_unused` | 스택 패턴 검사에서 관측한 미사용 바이트와 조회한 값들의 최솟값. 조회 실패는 `unavailable=<negative errno>` |

collector의 짧은 `k_spinlock` 구간에서 카운터·snapshot을 보호한다. 그 안에서는 시계 조회·큐 API·스택 검사·로그를 실행하지 않는다. handler만 측정 begin/end를 호출하며 중첩 호출을 지원하지 않는다. 제출 여부와 무관하게 종료 지점에서 문맥을 해제한다. INIT/RESET·무수신 timeout의 입력 없는 TRACK은 측정 지연에 포함하지 않는다.

카운터는 UINT32_MAX에서 포화한다. 시간 통계는 count가 포화되면 합계·히스토그램·최대값을 함께 동결한다. 한 시간 표본도 UINT32_MAX ms로 제한하여 uint64 누적 합계가 넘치지 않게 한다. INIT/RESET은 이 통계를 지우지 않는다. 새 run은 재부팅으로 시작한다.

통계 구조체는 한 번의 잠금으로 복사하지만 여러 단계의 이벤트는 서로 다른 순간에 기록된다. 입력·처리·송신이 진행 중일 때 카운터가 즉시 일치하지 않을 수 있다. 스택 검사는 snapshot 이후 별도 조회이며 동시에 실행 중인 스레드의 사용을 샘플링한다. 무손실·완전한 peak 검출이나 stack overflow 부재를 보장하지 않는다.

collector는 `src/diagnostics/perf_monitor.c/.h`, 출력·스택 조회는 `perf_reporter.c/.h`에 둔다. main은 `perf_reporter.h`의 `perf_reporter_log_summary()`를 호출한다. 별도 계측 스레드를 만들지 않고 기존 main 루프를 사용한다. 큰 출력용 snapshot은 main 스택 대신 reporter의 정적 저장소에 둔다. 로그 모듈 `radar_perf`는 고정 INFO다. 요약은 여러 LOG 호출이므로 다른 로그와 섞이거나 버퍼 포화 시 일부가 누락될 수 있다.

## 검증과 다음 보드 시험

host 사례와 실행 명령은 [성능 계측 테스트](../fcc_app/tests/test_perf_monitor/README.md)에 정리했다. 요구사항은 [REQ-PERF-001](requirements-traceability.md#spec-perf)로 추적한다.

1. `perf.conf`를 플래시하고 ST-Link 콘솔 원문을 저장한다. 처음에는 입력 없이 0 표본·세 스택 조회 결과를 확인한다.
2. 짧은 정상 입력에서 queue_wait/processing/rx_to_submit 표본 증가, 20ms 초과 횟수, STATUS/TRACK PC 수신과 TX 집계를 대조한다.
3. INIT/RESET·무수신 timeout 보고가 측정 표본을 추가하지 않는지, 오래된/무효 입력·큐 실패가 각 카운터에 반영되는지 확인한다.
4. 별도 run에서 README의 선언 부하(초안: 50Hz 측정·50Hz 결과·1Hz STATUS)를 30분 적용한다. 시작/종료 설정·baud·프레임 수·마지막 누적 요약·RX STATUS·PC CSV를 함께 남긴다.
5. 송신 중단 후 큐가 비워진 구간의 통계를 비교한다. timeout 이벤트 TRACK 등 입력 없는 보고도 TX 프레임 수에 포함해 설명한다. 계측 로그 유실/타임스탬프 이상이 있으면 유효한 성능 증거로 단정하지 않는다.
6. 일반 이미지를 복원하고 기본 왕복을 확인한다.

계측 구현·host/빌드·짧은 실보드 확인과 30분 관측 기록은 확보했다. 다음은 PC 왕복 지연을 중심으로 개선 전후를 비교하고 최종 구성의 오류 회귀·재현 자료를 정리하는 것이다. 호스트 공백 제거만을 위한 장시간 재시험은 요구하지 않는다.
