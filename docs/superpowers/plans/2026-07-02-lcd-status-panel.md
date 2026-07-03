# LCD 텍스트 상태판 (LVGL) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** F746G-DISCO 온보드 480×272 LCD에 HIL 최신 측정값·프레임 카운터를 LVGL 텍스트로 실시간 표시한다.

**Architecture:** tracking 스레드가 프레임마다 공유 스냅샷 `g_hil_stats`(mutex 보호)를 갱신하고, display 스레드가 LVGL 라벨로 그 스냅샷을 주기 렌더한다. 트랙 위치는 Kalman 미포팅이라 `tracks: 0 (kalman TODO)`로 정직하게 표기.

**Tech Stack:** Zephyr v4.2, LVGL(Zephyr 통합 `CONFIG_LVGL`), C, STM32F746G-DISCO LTDC LCD.

## Global Constraints

- 보드: `stm32f746g_disco`. RTOS: Zephyr v4.2. SDK `~/zephyr-sdk-1.0.1`, workspace `~/zephyrproject`.
- 작업 전 항상: `source ~/zephyrproject/.venv/bin/activate`.
- 빌드: `cd ~/zephyrproject && west build -p always -b stm32f746g_disco /Users/yuseungjae/Desktop/radar-track-fcc-hil/fcc_app`
- 플래시: `west flash --runner openocd` (반드시 openocd 러너).
- 콘솔: `tio /dev/ttyACM0` (115200 8N1).
- 인라인 주석/문서는 한국어.
- 인증 아티팩트 자동생성 금지.
- **테스트 수단:** fcc_app엔 C 유닛테스트 하네스 없음. 온-디바이스 LVGL은 호스트 자동테스트 불가하므로 각 태스크 검증 = `west build` 성공 + (보드-가시 태스크는) 플래시 후 육안 확인. ztest 하네스 신규 도입 금지(YAGNI).

## File Structure

- Modify `fcc_app/src/tracking.h` — `struct hil_stats` + `extern g_hil_stats` / `g_stats_mutex` 선언.
- Modify `fcc_app/src/tracking.c` — 스냅샷 정의 + 프레임마다 갱신.
- Modify `fcc_app/prj.conf` — LVGL/DISPLAY 설정.
- Rewrite `fcc_app/src/display.c` — LVGL 초기화 + 라벨 렌더.
- Modify `fcc_app/src/main.c` — display 스레드 스택 증가(LVGL용).
- Modify `README.md`, `CLAUDE.md` — 진행 상태 정직 갱신.

---

### Task 1: 공유 통계 스냅샷 (`g_hil_stats`) + tracking 갱신

display가 읽을 측정 데이터 통로를 만든다. tracking이 프레임마다 write. 이 태스크는 아직 display를 건드리지 않음 — 스냅샷 배관만.

**Files:**
- Modify: `fcc_app/src/tracking.h`
- Modify: `fcc_app/src/tracking.c`

**Interfaces:**
- Consumes: `FccMeasPayload`(필드 `timestamp_ms`, `range_m`, `azimuth_rad`, `doppler_mps` — `frame.h`), `meas_msgq`(`uart_rx.h`).
- Produces:
  - `struct hil_stats { uint32_t frames_rx; bool has_meas; uint32_t last_ts_ms; float last_range_m; float last_azimuth_rad; float last_doppler_mps; };`
  - `extern struct hil_stats g_hil_stats;`
  - `extern struct k_mutex g_stats_mutex;`

- [ ] **Step 1: `tracking.h`에 스냅샷 타입·extern 추가**

`struct track_table` 선언 뒤, `extern struct track_table g_track_table;` 블록 근처에 추가:

```c
/* display 스레드에 넘길 HIL 통계 스냅샷 (g_stats_mutex로 보호) */
struct hil_stats {
    uint32_t frames_rx;         /* 누적 수신 측정 프레임 수 */
    bool     has_meas;          /* 측정 1건 이상 수신했는가 */
    uint32_t last_ts_ms;        /* 마지막 측정 timestamp_ms */
    float    last_range_m;
    float    last_azimuth_rad;
    float    last_doppler_mps;
};

extern struct hil_stats g_hil_stats;
extern struct k_mutex   g_stats_mutex;
```

- [ ] **Step 2: `tracking.c`에 스냅샷 정의**

`K_MUTEX_DEFINE(g_track_mutex);` 아래 줄에 추가:

```c
/* HIL 통계 스냅샷 (display 스레드가 read) */
struct hil_stats g_hil_stats;
K_MUTEX_DEFINE(g_stats_mutex);
```

- [ ] **Step 3: tracking 루프에서 스냅샷 갱신**

`tracking_thread`의 `printk(...)` 호출 직후, TX 블록(`if (tx_ready)`) 앞에 삽입:

```c
        /* display용 통계 스냅샷 갱신 */
        k_mutex_lock(&g_stats_mutex, K_FOREVER);
        g_hil_stats.frames_rx        = cnt + 1U;
        g_hil_stats.has_meas         = true;
        g_hil_stats.last_ts_ms       = mp.timestamp_ms;
        g_hil_stats.last_range_m     = mp.range_m;
        g_hil_stats.last_azimuth_rad = mp.azimuth_rad;
        g_hil_stats.last_doppler_mps = mp.doppler_mps;
        k_mutex_unlock(&g_stats_mutex);
```

- [ ] **Step 4: 빌드 검증**

Run:
```bash
source ~/zephyrproject/.venv/bin/activate
cd ~/zephyrproject && west build -p always -b stm32f746g_disco /Users/yuseungjae/Desktop/radar-track-fcc-hil/fcc_app
```
Expected: `Memory region ... FLASH ...` 요약과 함께 빌드 성공, 에러 0. (`g_hil_stats` 미사용 경고 없음 — extern 노출됨.)

- [ ] **Step 5: 커밋**

```bash
git add fcc_app/src/tracking.h fcc_app/src/tracking.c
git commit -m "feat(fcc): display용 HIL 통계 스냅샷 g_hil_stats 추가

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 2: LVGL 활성화 + 정적 라벨 (보드에 뜨는지 검증)

가장 위험한 미지수 = "이 보드에서 LVGL이 LCD에 올라오나"를 데이터 배관과 분리해 먼저 검증. 정적 텍스트만 띄운다.

**Files:**
- Modify: `fcc_app/prj.conf`
- Modify: `fcc_app/src/main.c` (display 스택 증가)
- Rewrite: `fcc_app/src/display.c`

**Interfaces:**
- Consumes: `zephyr,display` chosen 노드(보드 devicetree 기본 제공), LVGL API.
- Produces: `void display_thread(...)` (시그니처 불변, `display.h` 그대로).

- [ ] **Step 1: `prj.conf`에 LVGL/DISPLAY 설정 추가**

파일 끝에 추가:

```
# LCD 상태판 (LVGL)
CONFIG_DISPLAY=y
CONFIG_LVGL=y
CONFIG_LV_Z_MEM_POOL_SIZE=16384
CONFIG_LV_USE_LABEL=y
CONFIG_LV_FONT_MONTSERRAT_14=y
CONFIG_LV_FONT_DEFAULT_MONTSERRAT_14=y

# %f 포맷 지원 (snprintf 부동소수)
CONFIG_CBPRINTF_FP_SUPPORT=y

# LVGL 위젯 힙
CONFIG_HEAP_MEM_POOL_SIZE=32768
```

- [ ] **Step 2: display 스레드 스택 증가**

`fcc_app/src/main.c`의 `#define STACK_DISPLAY  2048` 를 아래로 변경 (LVGL은 2048로 부족):

```c
#define STACK_DISPLAY  8192
```

- [ ] **Step 3: `display.c`를 LVGL 정적 라벨로 재작성**

`fcc_app/src/display.c` 전체를 아래로 교체:

```c
/*
 * display.c — display 스레드 (LVGL)
 * F746G-DISCO 온보드 LCD에 HIL 상태판 렌더.
 * 이 단계: 정적 "RADAR FCC (HIL)" 라벨만 표시 (LVGL 배선 검증용).
 */

#include "display.h"
#include "tracking.h"
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/sys/printk.h>
#include <lvgl.h>

void display_thread(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

    const struct device *disp = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
    if (!device_is_ready(disp)) {
        printk("[display ] LCD 디바이스 미준비 — 렌더 중단\n");
        return;
    }

    /* 좌상단 정렬 멀티라인 라벨 1개 */
    lv_obj_t *label = lv_label_create(lv_scr_act());
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 8, 8);
    lv_label_set_text(label, "RADAR FCC (HIL)\nbooting...");

    display_blanking_off(disp);

    while (1) {
        lv_task_handler();
        k_sleep(K_MSEC(30));
    }
}
```

- [ ] **Step 4: 빌드 검증**

Run:
```bash
cd ~/zephyrproject && west build -p always -b stm32f746g_disco /Users/yuseungjae/Desktop/radar-track-fcc-hil/fcc_app
```
Expected: 빌드 성공. LVGL·display 드라이버 링크됨. 에러 0.

- [ ] **Step 5: 플래시 + 육안 검증**

Run:
```bash
west flash --runner openocd
```
Expected: 보드 LCD에 `RADAR FCC (HIL)` / `booting...` 텍스트가 좌상단에 표시. 화면 백라이트 켜짐. 아무것도 안 뜨면 → `zephyr,display` chosen 노드/LVGL config 재점검(진행 중단하고 보고).

- [ ] **Step 6: 커밋**

```bash
git add fcc_app/prj.conf fcc_app/src/main.c fcc_app/src/display.c
git commit -m "feat(fcc): LVGL 활성화 + LCD 정적 상태판 라벨

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 3: 스냅샷 실시간 렌더

`g_hil_stats`를 읽어 멀티라인 텍스트를 주기 갱신. 측정 전엔 `--`, azimuth는 deg 변환.

**Files:**
- Modify: `fcc_app/src/display.c`

**Interfaces:**
- Consumes: `g_hil_stats`, `g_stats_mutex` (Task 1), LVGL 라벨(Task 2).
- Produces: 없음 (최종 렌더 로직).

- [ ] **Step 1: `display.c` 렌더 루프 확장**

`#include <lvgl.h>` 아래에 표준 라이브러리·상수 추가:

```c
#include <stdio.h>
#include <math.h>

#define RAD2DEG(r) ((r) * 180.0f / (float)M_PI)
```

`while (1)` 루프를 아래로 교체 (Task 2의 정적 `lv_label_set_text` 초기값은 유지):

```c
    char buf[256];

    while (1) {
        /* 스냅샷 복사 (락 최소 구간) */
        struct hil_stats s;
        k_mutex_lock(&g_stats_mutex, K_FOREVER);
        s = g_hil_stats;
        k_mutex_unlock(&g_stats_mutex);

        if (s.has_meas) {
            snprintf(buf, sizeof(buf),
                     "RADAR FCC (HIL)\n"
                     "frames rx : %u\n"
                     "last meas\n"
                     "  t   : %u ms\n"
                     "  rng : %.1f m\n"
                     "  az  : %.1f deg\n"
                     "  dop : %.1f m/s\n"
                     "tracks: 0 (kalman TODO)",
                     s.frames_rx, s.last_ts_ms,
                     (double)s.last_range_m,
                     (double)RAD2DEG(s.last_azimuth_rad),
                     (double)s.last_doppler_mps);
        } else {
            snprintf(buf, sizeof(buf),
                     "RADAR FCC (HIL)\n"
                     "frames rx : 0\n"
                     "last meas\n"
                     "  t   : --\n"
                     "  rng : --\n"
                     "  az  : --\n"
                     "  dop : --\n"
                     "tracks: 0 (kalman TODO)");
        }
        lv_label_set_text(label, buf);

        lv_task_handler();
        k_sleep(K_MSEC(100));
    }
```

주의: `label` 변수는 Task 2에서 루프 앞에 선언됨. `buf`도 루프 앞으로 옮겼으니 Task 2의 `lv_label_set_text(label, "RADAR FCC (HIL)\nbooting...");` 줄은 그대로 두어 초기 프레임 표시.

- [ ] **Step 2: 빌드 검증**

Run:
```bash
cd ~/zephyrproject && west build -p always -b stm32f746g_disco /Users/yuseungjae/Desktop/radar-track-fcc-hil/fcc_app
```
Expected: 빌드 성공, 에러 0. `snprintf` %f 경고 없음(FP 지원 켜짐).

- [ ] **Step 3: 플래시**

```bash
west flash --runner openocd
```
Expected: 부팅 직후 LCD에 `frames rx : 0`, `last meas` 값 전부 `--`.

- [ ] **Step 4: HIL 실측 검증**

터미널 2개. #1 콘솔:
```bash
tio /dev/ttyACM0
```
#2 시뮬레이터:
```bash
source ~/zephyrproject/.venv/bin/activate
cd /Users/yuseungjae/Desktop/radar-track-fcc-hil/pc_sim
python run_sim.py --serial /dev/ttyUSB0 --scenario single_approach
```
Expected:
- LCD `frames rx` 값이 증가.
- LCD `rng`/`az`/`dop` 값이 콘솔 `[tracking] #N t=... r=... az=...` 최신 로그와 일치(az는 콘솔 rad, LCD deg — `deg = rad*180/π` 대조).
- 시나리오 종료 후 카운터 정지.
불일치 시 진행 중단하고 보고.

- [ ] **Step 5: 커밋**

```bash
git add fcc_app/src/display.c
git commit -m "feat(fcc): LCD 상태판에 최신 측정값·프레임 카운터 실시간 렌더

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 4: 진행 상태 문서 갱신

구현 완료를 CLAUDE.md/README 진행 상태에 정직 반영.

**Files:**
- Modify: `CLAUDE.md`
- Modify: `README.md`

**Interfaces:** 없음 (문서).

- [ ] **Step 1: `CLAUDE.md` 진행 상태 갱신**

`- [ ] LVGL PPI 디스플레이` 줄을 아래로 교체:

```
- [x] LVGL 상태판(텍스트): 최신 측정·프레임 카운터 LCD 표시
- [ ] LVGL PPI 디스플레이 (트랙 스코프) ← Kalman 이후
```

- [ ] **Step 2: `README.md` 진행 상태 갱신**

`- [ ] LVGL PPI 디스플레이` 줄을 아래로 교체:

```
- [x] LVGL 상태판(텍스트): 최신 측정·프레임 카운터 LCD 표시
- [ ] LVGL PPI 디스플레이 (트랙 스코프) — Kalman 이후
```

- [ ] **Step 3: 커밋**

```bash
git add CLAUDE.md README.md
git commit -m "docs: 진행 상태 — LVGL 텍스트 상태판 완료 반영

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Self-Review 결과

- **Spec 커버리지:** 데이터 배관 A안(Task 1) / LVGL 렌더(Task 2-3) / 화면 레이아웃(Task 3) / 검증 방법(Task 3 Step 4) / 진행 상태 갱신(Task 4) — 전부 태스크로 매핑됨. `--` 미측정 표기(Task 3), rad→deg(Task 3) 포함.
- **플레이스홀더:** 없음. 모든 코드 단계 실제 코드 포함.
- **타입 일관성:** `struct hil_stats`/`g_hil_stats`/`g_stats_mutex` 이름이 Task 1 정의와 Task 3 사용에서 동일. `label`/`buf` 변수 Task 2→3 연속성 명시.
- **알려진 리스크:** LVGL config 키(`CONFIG_LV_*`)는 Zephyr 4.2 기준. 빌드 시 Kconfig 이름 불일치 나면 `west build` 에러 메시지로 정확한 심볼 확인 후 수정(예: 폰트/메모리 심볼). Task 2 Step 5에서 LCD 무출력 시 중단·보고.
