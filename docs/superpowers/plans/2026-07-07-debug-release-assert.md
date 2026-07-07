# Debug/Release 빌드 분리 + assert 디버깅 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `fcc_app`을 debug/release 오버레이 빌드로 나누고, debug에서만 동작하는 assert를 kalman.c/frame.c 핵심 계약에 추가한다.

**Architecture:** Zephyr overlay conf 방식. `prj.conf`는 공통 베이스 유지, `debug.conf`/`release.conf`를 `-DEXTRA_CONF_FILE`로 선택. assert는 `__ASSERT()`(Zephyr) 사용, `CONFIG_ASSERT`로 debug 빌드에서만 코드 확장. frame.c는 host 이식성을 위해 `__ZEPHYR__` 가드로 표준 `assert` 폴백.

**Tech Stack:** Zephyr v4.2, west, ztest(native_sim), STM32F746G-DISCO.

## Global Constraints

- 보드 ID: `stm32f746g_disco` (verbatim)
- 빌드: `west build -p always -b stm32f746g_disco fcc_app` 형태
- 작업 전: `source ~/zephyrproject/.venv/bin/activate`
- 인라인 주석/문서: 한국어
- assert는 "일어나면 안 되는 프로그래머 버그"만. 정상 입력 방어(`if ... return`)는 그대로 유지.
- `FCC_MAX_PAYLOAD` = 64 (frame.h, verbatim)

---

### Task 1: overlay conf 파일 추가 + 빌드 분리 검증

**Files:**
- Create: `fcc_app/debug.conf`
- Create: `fcc_app/release.conf`

**Interfaces:**
- Consumes: 기존 `fcc_app/prj.conf` (공통 베이스, 수정 안 함)
- Produces: `debug.conf`(CONFIG_ASSERT=y), `release.conf`(CONFIG_ASSERT 미설정) — Task 2/3의 assert가 debug에서만 확장되는 근거.

- [ ] **Step 1: `fcc_app/debug.conf` 작성**

```
# 디버그 빌드 오버레이 — assert 활성화 + 디버그 심볼 + 상세 로그
CONFIG_ASSERT=y
CONFIG_ASSERT_LEVEL=2
CONFIG_DEBUG=y
CONFIG_DEBUG_THREAD_INFO=y
CONFIG_LOG_DEFAULT_LEVEL=4
```

- [ ] **Step 2: `fcc_app/release.conf` 작성**

```
# 릴리즈 빌드 오버레이 — assert 제거 + 속도 최적화 + 로그 최소화
CONFIG_ASSERT=n
CONFIG_SPEED_OPTIMIZATIONS=y
CONFIG_LOG_DEFAULT_LEVEL=2
```

- [ ] **Step 3: debug 빌드**

Run:
```bash
source ~/zephyrproject/.venv/bin/activate
west build -p always -b stm32f746g_disco fcc_app -- -DEXTRA_CONF_FILE=debug.conf
```
Expected: 빌드 성공 (`Memory region ... FLASH ...` 출력).

- [ ] **Step 4: debug .config에 ASSERT=y 확인**

Run: `grep CONFIG_ASSERT= build/zephyr/.config`
Expected: `CONFIG_ASSERT=y`

- [ ] **Step 5: release 빌드**

Run:
```bash
west build -p always -b stm32f746g_disco fcc_app -- -DEXTRA_CONF_FILE=release.conf
```
Expected: 빌드 성공.

- [ ] **Step 6: release .config에 ASSERT 꺼짐 확인**

Run: `grep -c "CONFIG_ASSERT=y" build/zephyr/.config || true`
Expected: `0` (ASSERT=y 라인 없음).

- [ ] **Step 7: Commit**

```bash
git add fcc_app/debug.conf fcc_app/release.conf
git commit -m "build(fcc): debug/release 오버레이 conf 분리 (assert on/off)"
```

---

### Task 2: kalman.c 핵심 수치/계약 assert 추가

**Files:**
- Modify: `fcc_app/src/kalman.c` (include 1줄 + 각 함수 assert)
- Modify: `fcc_app/tests/test_kalman/prj.conf` (`CONFIG_ASSERT=y` 추가)

**Interfaces:**
- Consumes: `struct kalman_state` (kalman.h — `float x[4]`, `float P[4][4]`), `kalman_init/predict/update` 시그니처 (기존).
- Produces: 없음 (내부 assert만 추가, 공개 API 불변).

- [ ] **Step 1: 테스트 prj.conf에 ASSERT 활성화**

`fcc_app/tests/test_kalman/prj.conf`를 다음으로 만든다:
```
CONFIG_ZTEST=y
CONFIG_ASSERT=y
```
이유: native_sim 테스트를 assert 켠 채 돌려 "정상 입력에 assert 오발 안 함"을 검증.

- [ ] **Step 2: 테스트가 여전히 통과하는지 먼저 확인 (baseline)**

Run:
```bash
source ~/zephyrproject/.venv/bin/activate
west twister -p native_sim -T fcc_app/tests/test_kalman --inline-logs
```
Expected: PASS (assert 켰지만 아직 assert 없음 → 기존 테스트 그대로 통과).

- [ ] **Step 3: kalman.c 상단에 assert 헤더 include**

`fcc_app/src/kalman.c`의 `#include <string.h>` 다음 줄에 추가:
```c
#include <zephyr/sys/__assert.h>
```

- [ ] **Step 4: `kalman_init`에 assert 추가**

함수 본문 첫 줄(`float sin_az = ...` 위)에 삽입:
```c
    __ASSERT(ks != NULL, "kalman_init: ks is NULL");
    __ASSERT(range_m >= 0.0f, "kalman_init: range_m < 0 (%f)", (double)range_m);
```

- [ ] **Step 5: `kalman_predict`에 assert 추가**

`float dt = dt_s;` 위에 삽입:
```c
    __ASSERT(ks != NULL, "kalman_predict: ks is NULL");
    __ASSERT(dt_s >= 0.0f, "kalman_predict: dt_s < 0 (%f)", (double)dt_s);
```

- [ ] **Step 6: `kalman_update` 진입 assert 추가**

`float x = ks->x[0];` 위에 삽입:
```c
    __ASSERT(ks != NULL, "kalman_update: ks is NULL");
    __ASSERT(range_m >= 0.0f, "kalman_update: range_m < 0 (%f)", (double)range_m);
```

- [ ] **Step 7: `kalman_update` 종료 assert 추가**

함수 맨 끝(공분산 갱신 `ks->P[i][j] -= KHP[i][j];` 이중 루프 닫은 직후, 함수 `}` 바로 위)에 삽입:
```c
    /* 발산/수치붕괴 조기 포착 (debug 전용) */
    for (int i = 0; i < 4; i++) {
        __ASSERT(isfinite(ks->x[i]), "kalman_update: state[%d] not finite", i);
    }
    __ASSERT(ks->P[0][0] >= 0.0f && ks->P[1][1] >= 0.0f &&
             ks->P[2][2] >= 0.0f && ks->P[3][3] >= 0.0f,
             "kalman_update: negative covariance diagonal");
```
(`isfinite`는 이미 include된 `<math.h>` 제공.)

- [ ] **Step 8: assert 켠 채 테스트 재실행 (정상 입력에 오발 없음 검증)**

Run:
```bash
west twister -p native_sim -T fcc_app/tests/test_kalman --inline-logs
```
Expected: PASS. (정상 시나리오 입력에 새 assert가 발동하지 않음 = 계약 위반 없음 확인.)

- [ ] **Step 9: assert 발동 스모크 테스트 (음수 dt) 추가**

`fcc_app/tests/test_kalman/src/main.c` 맨 끝(마지막 `ZTEST` 다음)에 추가:
```c
/* debug 빌드에서 잘못된 입력이 assert를 발동시키는지 확인.
 * ztest_expect_assert로 __ASSERT 실패를 정상 통과로 처리. */
ZTEST(kalman, test_negative_dt_triggers_assert)
{
    struct kalman_state ks;
    kalman_init(&ks, 1000.0f, 0.0f);

    ztest_set_assert_valid(true);   /* 다음 __ASSERT 실패는 기대된 것 */
    kalman_predict(&ks, -1.0f);     /* dt<0 → assert 발동해야 함 */
    ztest_set_assert_valid(false);
}
```

- [ ] **Step 10: assert 발동 테스트 실행**

Run:
```bash
west twister -p native_sim -T fcc_app/tests/test_kalman --inline-logs
```
Expected: PASS. `test_negative_dt_triggers_assert`가 assert 발동을 잡아 통과.
(만약 `ztest_set_assert_valid` 미지원으로 링크 에러 시: 해당 테스트를 제거하고, assert 발동 확인은 스펙의 "선택 검증(보드 수동)"으로 남긴다 — 커밋 메시지에 명시.)

- [ ] **Step 11: Commit**

```bash
git add fcc_app/src/kalman.c fcc_app/tests/test_kalman/prj.conf fcc_app/tests/test_kalman/src/main.c
git commit -m "feat(fcc): kalman.c 핵심 수치/계약 assert 추가 (debug 전용)"
```

---

### Task 3: frame.c 버퍼 경계/NULL assert 추가

**Files:**
- Modify: `fcc_app/src/frame.c` (assert 헤더 가드 + 각 함수 assert)

**Interfaces:**
- Consumes: `FrameParser`(`buf[FCC_MAX_PAYLOAD]`, `buf_idx`), `FccFrame`, `frame_parser_feed`/`fcc_encode_frame` 시그니처 (frame.h 기존).
- Produces: 없음 (내부 assert만).

- [ ] **Step 1: frame.c 상단에 assert 가드 include 추가**

`fcc_app/src/frame.c`의 기존 include 블록 다음에 삽입 (BUILD_ASSERT 라인 위):
```c
/* host 유닛테스트 이식성: 보드는 Zephyr __ASSERT, 그 외는 표준 assert */
#ifdef __ZEPHYR__
#include <zephyr/sys/__assert.h>
#else
#include <assert.h>
#define __ASSERT(cond, ...) assert(cond)
#endif
```

- [ ] **Step 2: `fcc_encode_frame`에 assert 추가**

함수 본문 첫 줄에 삽입:
```c
    __ASSERT(buf != NULL, "fcc_encode_frame: buf is NULL");
    __ASSERT(payload_len <= FCC_MAX_PAYLOAD,
             "fcc_encode_frame: payload_len %u > max", payload_len);
```

- [ ] **Step 3: `frame_parser_feed`에 NULL assert 추가**

함수 본문 첫 줄에 삽입:
```c
    __ASSERT(p != NULL, "frame_parser_feed: p is NULL");
    __ASSERT(out != NULL, "frame_parser_feed: out is NULL");
```

- [ ] **Step 4: payload 기록 지점에 오버런 방지 assert 추가**

`frame_parser_feed` 안에서 `p->buf[p->buf_idx]`에 바이트를 기록하는 지점을 찾아, 대입 직전에 삽입:
```c
        __ASSERT(p->buf_idx < FCC_MAX_PAYLOAD,
                 "frame_parser_feed: buf overrun (idx=%u)", p->buf_idx);
```
(정확한 위치는 `FP_WAIT_PAYLOAD` 케이스의 `p->buf[p->buf_idx++] = b;` 형태 라인 직전.)

- [ ] **Step 5: debug 빌드로 컴파일 검증**

Run:
```bash
source ~/zephyrproject/.venv/bin/activate
west build -p always -b stm32f746g_disco fcc_app -- -DEXTRA_CONF_FILE=debug.conf
```
Expected: 빌드 성공 (assert 포함 컴파일).

- [ ] **Step 6: release 빌드로 assert 제거 검증**

Run:
```bash
west build -p always -b stm32f746g_disco fcc_app -- -DEXTRA_CONF_FILE=release.conf
```
Expected: 빌드 성공 (`__ASSERT` 확장 안 됨, 경고 없음).

- [ ] **Step 7: Commit**

```bash
git add fcc_app/src/frame.c
git commit -m "feat(fcc): frame.c 버퍼 경계/NULL assert 추가 (debug 전용)"
```

---

### Task 4: 보드 통합 확인 (debug 플래시 스모크)

**Files:** 없음 (검증 전용 태스크).

**Interfaces:**
- Consumes: Task 1~3의 debug 빌드 산출물.
- Produces: 보드에서 assert 미발동 정상 동작 확인 (문서화용).

- [ ] **Step 1: debug 빌드 재확인**

Run:
```bash
source ~/zephyrproject/.venv/bin/activate
west build -p always -b stm32f746g_disco fcc_app -- -DEXTRA_CONF_FILE=debug.conf
```
Expected: 성공.

- [ ] **Step 2: 플래시 (openocd 러너)**

Run: `west flash --runner openocd`
Expected: `** Verified OK **` / `** Resetting Target **`.

- [ ] **Step 3: 콘솔에서 assert 배너 없음 확인**

`tio /dev/ttyACM0` 연결 후 정상 부팅 로그 관찰.
Expected: `ASSERTION FAIL` 배너 **없이** 기존 스레드 로그(`[tracking] ...` 등) 정상 출력. (정상 입력에 assert 미발동 = 계약 준수 확인.)

- [ ] **Step 4: 진행상태 문서 갱신 커밋 (선택)**

CLAUDE.md 진행상태에 빌드 분리/assert 항목 반영이 필요하면 이 단계에서. 아니면 생략.

---

## Self-Review

- **Spec coverage:** 빌드 분리(Task 1) ✔, kalman assert(Task 2, 5개 지점 전부) ✔, frame assert(Task 3, 3개 지점) ✔, `__ZEPHYR__` 가드(Task 3 Step 1) ✔, 검증 사다리(Task 1 build 확인 / Task 2 native_sim / Task 4 보드) ✔.
- **Placeholder scan:** 코드 스텝 전부 실제 코드 포함. Task 3 Step 4는 정확 라인이 소스 의존이라 "찾아서 직전 삽입" 지시 + 형태 명시 — 플레이스홀더 아님.
- **Type consistency:** `struct kalman_state`/`ks->x[i]`/`ks->P[i][j]`, `FrameParser`/`p->buf_idx`/`FCC_MAX_PAYLOAD`, `frame_parser_feed(p, b, out)` — frame.h/kalman.h 실제 정의와 일치.
- **리스크:** `ztest_set_assert_valid` 미지원 가능성 → Task 2 Step 10에 폴백 명시.
