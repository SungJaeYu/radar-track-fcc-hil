# Debug/Release 빌드 분리 + assert 디버깅

날짜: 2026-07-07
대상: `fcc_app` (STM32F746G-DISCO / Zephyr v4.2)

## 목표

빌드 환경을 debug/release로 나누고, debug 빌드에서만 `__ASSERT()`가
동작하도록 한다. 핵심 수치/계약 위반을 조기에 잡는 assert를
`kalman.c`, `frame.c`에 추가한다.

핵심 포지셔닝("검증을 아는 개발자")과 정렬: assert는 트래킹 수치
발산과 프레임 버퍼 경계 위반을 개발 단계에서 정량적으로 드러낸다.

## 비목표 (YAGNI)

- sysbuild / build type(FILE_SUFFIX) 도입 — 이 규모엔 과함.
- tracking.c / uart_rx.c assert — 아직 골격 단계. 이번 범위 밖.
- 런타임 정상 입력 이상(가비지 프레임, 특이행렬)을 assert로 전환 —
  기존 방어 로직 유지.
- 인증 아티팩트 자동생성 — 별도 트랙.

## 1. 빌드 환경 분리 (overlay conf)

`prj.conf`는 공통 베이스로 그대로 둔다. 두 오버레이 파일 신규 추가.

### `fcc_app/debug.conf`
```
CONFIG_ASSERT=y              # __ASSERT() 코드 생성 활성화
CONFIG_ASSERT_LEVEL=2        # 최고 수준
CONFIG_DEBUG=y               # -Og + 디버그 심볼
CONFIG_DEBUG_THREAD_INFO=y   # 스레드 인지 디버깅
CONFIG_LOG_DEFAULT_LEVEL=4   # DBG 로그까지
```

### `fcc_app/release.conf`
```
CONFIG_ASSERT=n              # assert 코드 제거
CONFIG_SPEED_OPTIMIZATIONS=y # -O2
CONFIG_LOG_DEFAULT_LEVEL=2   # WRN 이상만
```

### 빌드 명령
```
# 디버그
west build -p always -b stm32f746g_disco fcc_app -- -DEXTRA_CONF_FILE=debug.conf
# 릴리즈
west build -p always -b stm32f746g_disco fcc_app -- -DEXTRA_CONF_FILE=release.conf
```

`__ASSERT()`는 `CONFIG_ASSERT=y`에서만 코드로 확장된다. release
빌드엔 명령 흔적이 남지 않는다.

## 2. assert 지점 (핵심 수치/계약)

Zephyr `__ASSERT(cond, msg, ...)` 사용 (`<zephyr/sys/__assert.h>`).
"일어나면 안 되는 프로그래머 버그"만 대상. 정상 입력 방어 로직은
`if`로 유지한다.

### `kalman.c`
| 함수 | assert |
|---|---|
| `kalman_init` | `ks != NULL`; `range_m >= 0.0f` |
| `kalman_predict` | `ks != NULL`; `dt_s >= 0.0f` (음수 dt = 시간 역행 버그) |
| `kalman_update` | `ks != NULL`; 진입 시 `range_m >= 0.0f` |
| `kalman_update` 종료 | `x[0..3]` 전부 `isfinite` (NaN/Inf = 발산) |
| `kalman_update` 종료 | 대각 `P[i][i] >= 0.0f` (음수 = 수치 붕괴) |

`isfinite`는 `<math.h>`. 종료 assert는 상태/공분산 갱신 직후.

### `frame.c`
| 함수 | assert |
|---|---|
| `frame_parser_feed` | `p != NULL`; `out != NULL` |
| payload 바이트 기록 전 | `p->buf_idx < FCC_MAX_PAYLOAD` (버퍼 오버런 방지) |
| `fcc_encode_frame` | `buf != NULL`; `payload_len <= FCC_MAX_PAYLOAD` |

## 검증 방법 (보드에서 확인)

작은 검증 사다리:

1. **빌드 분리 검증**
   - debug/release 두 빌드 모두 성공하는지 확인.
   - `build/zephyr/.config`에서 `CONFIG_ASSERT` 값이 debug=y,
     release 미설정(=n)인지 확인.

2. **assert 미발동 정상 동작**
   - debug 빌드 플래시 → `tio /dev/ttyACM0` 콘솔.
   - 정상 시나리오(`run_sim.py --scenario single_approach`)에서
     assert 배너 없이 트래킹 로그 정상 출력.

3. **assert 발동 확인 (선택)**
   - 임시로 kalman에 잘못된 입력(음수 dt 등)을 흘려 debug
     빌드에서 `ASSERTION FAIL` 배너 + 파일/라인 출력 확인.
   - release 빌드에선 동일 입력에 배너 없이 진행(코드 제거됨) 확인.

## 파일 변경 요약

- 신규: `fcc_app/debug.conf`, `fcc_app/release.conf`
- 수정: `fcc_app/src/kalman.c` (assert 추가, `<zephyr/sys/__assert.h>` include)
- 수정: `fcc_app/src/frame.c` (assert 추가) — 단, frame.c는 "Zephyr
  비의존 순수 C" 주석이 있으므로 include 처리 주의 (아래 참고).

### frame.c Zephyr 의존성 처리

`frame.h`는 "Zephyr 비의존, 순수 C"를 표방하지만, `frame.c`는 이미
`BUILD_ASSERT`(Zephyr 매크로)를 무조건 사용 중이라 사실상 Zephyr에
묶여 있다. `__ASSERT`도 Zephyr 헤더다. 이식성을 살리려면 frame.c
상단에서 가드한다:

```c
#ifdef __ZEPHYR__
#include <zephyr/sys/__assert.h>
#else
#include <assert.h>
#define __ASSERT(cond, ...) assert(cond)
#endif
```

이러면 PC 유닛테스트(호스트 컴파일) 환경에서도 표준 `assert`로
동작하고, 보드에선 Zephyr assert로 동작한다.
