# STM32 LCD 텍스트 상태판 (LVGL) — 설계

날짜: 2026-07-02
대상: `fcc_app` (STM32F746G-DISCO, Zephyr v4.2)

## 목적

F746G-DISCO 온보드 480×272 LTDC LCD에 HIL 상태를 실시간 표시한다.
지금 **실제 존재하는 데이터만** 정직하게 띄운다: 최신 측정값 + 프레임 카운터.
트랙 위치(x/y)는 Kalman 미포팅이라 없으므로 `tracks: 0 (kalman TODO)`로 명시한다.

이번 작업의 진짜 목적은 화면 채우기가 아니라 **LVGL 배선·디스플레이 파이프라인을
보드에서 검증**하는 것. 이후 PPI 스코프로 진화할 토대를 깐다.

## 현재 상태 (사실 확인)

- `display.c`: 2초마다 활성 트랙 수만 `printk`. LVGL 미사용.
- `struct track`: `active`, `id`만. 위치 없음.
- `g_track_table`: tracking 스레드가 한 번도 갱신하지 않음 → 항상 active=0.
- tracking 스레드: `meas_msgq`에서 `FccMeasPayload`(range/azimuth/doppler) 수신 →
  더미 `FccTrackPayload`(x=range) 에코 TX. 실제 트랙 없음.

즉 화면에 보여줄 수 있는 실데이터 = **최신 측정값 + 수신 프레임 수**뿐.

## 아키텍처

### 1. 데이터 배관 (채택: A안)

display 스레드가 tracking의 측정 데이터를 받을 통로가 필요하다.
`g_track_table`은 트랙만 담고 측정값이 없다.

**A안 (채택):** 공유 스냅샷 `g_hil_stats` + 전용 mutex `g_stats_mutex`.
- tracking 스레드가 프레임 처리 끝에서 lock 잡고 write.
- display 스레드가 갱신 주기마다 lock 잡고 read.
- 1-writer / 1-reader, 디커플드, 단순.

기각한 대안:
- B안 (두 번째 msgq 관찰): 측정은 이미 tracking이 소비. 과한 구조.
- C안 (volatile/atomic, 락 없음): float은 원자성 없음 → 값 찢김 위험.

```c
/* tracking.h */
struct hil_stats {
    uint32_t frames_rx;      /* 누적 수신 측정 프레임 수 */
    bool     has_meas;       /* 측정 1건 이상 수신했는가 */
    uint32_t last_ts_ms;     /* 마지막 측정 timestamp */
    float    last_range_m;
    float    last_azimuth_rad;
    float    last_doppler_mps;
};
extern struct hil_stats g_hil_stats;
extern struct k_mutex   g_stats_mutex;
```

### 2. 컴포넌트

- **prj.conf 추가**
  - `CONFIG_DISPLAY=y`
  - `CONFIG_LVGL=y`
  - LVGL 메모리 풀 / 라벨 위젯 / 기본 폰트 옵션 (빌드 시 필요한 최소만)
- **display.c 재작성**
  - `const struct device *disp = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));`
  - `device_is_ready` 확인 → `display_blanking_off(disp)`
  - LVGL 멀티라인 라벨 위젯 1개 생성 (화면 좌상단 정렬)
  - 루프: `g_stats_mutex` lock → 스냅샷 복사 → unlock →
    `lv_label_set_text_fmt(...)` → `lv_task_handler()` → `k_sleep(K_MSEC(30))`
  - 측정 전(`has_meas==false`)엔 값 자리에 `--` 표기
- **tracking.c 수정**
  - 파일 상단에 `struct hil_stats g_hil_stats;` / `K_MUTEX_DEFINE(g_stats_mutex);`
  - 측정 수신·에코 뒤 lock 잡고 `g_hil_stats` 갱신 (frames_rx++, last_* 대입, has_meas=true)

### 3. 화면 레이아웃

```
┌─ RADAR FCC (HIL) ──────┐
│ frames rx : 1420       │
│ last meas              │
│   t   : 14200 ms       │
│   rng : 1200.0 m       │
│   az  : 12.3 deg       │
│   dop : -4.5 m/s       │
│ tracks: 0 (kalman TODO)│
└────────────────────────┘
```

- 라벨 하나로 멀티라인 텍스트 (`\n` 구분).
- 폰트: LVGL 기본 montserrat.
- azimuth는 rad → deg 변환하여 표시 (`* 180 / PI`).

## 검증 방법 (보드)

1. 빌드·플래시 후 LCD에 상태판 프레임이 뜨는지.
   측정 전이면 `frames rx: 0`, `last meas` 값 = `--`.
2. `python run_sim.py --serial /dev/ttyUSB0 --scenario single_approach` 실행 →
   LCD의 `frames rx` 증가, `last meas` 값이 Zephyr 콘솔 `[tracking] #N ...`
   로그와 일치하는지 눈으로 대조.
3. 시나리오 종료 후 카운터가 멈추는지 확인.

## 범위 밖 (YAGNI)

터치 입력, PPI 그래픽/원형 스코프, 트랙 테이블 렌더, 측정 링버퍼 스크롤 —
전부 후속 작업. 이번엔 read-only 텍스트 상태판만.

## 진행 상태 반영

완료 시 CLAUDE.md / README 진행 상태의 "LVGL PPI 디스플레이" 항목을
"LVGL 상태판(텍스트) 완료 / PPI 후속"으로 정직하게 갱신한다.
