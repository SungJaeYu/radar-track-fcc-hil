# 보드 브링업 체크리스트 — LCD 텍스트 상태판 (LVGL)

이 문서는 LVGL 텍스트 상태판(`fcc_app/src/display.c`)을 **실제 보드에서 처음 빌드·플래시·검증**할 때
쓰는 체크리스트다. 코드는 이미 main에 머지·리뷰 완료됐으나, 개발 머신에 Zephyr 툴체인이
없어 **빌드·플래시·LCD·HIL 실측은 아직 안 됐다.** 별도 보드 머신에서 아래를 수행한다.

관련 문서:

- 설계: `docs/superpowers/specs/2026-07-02-lcd-status-panel-design.md`
- 구현 계획: `docs/superpowers/plans/2026-07-02-lcd-status-panel.md`
  (Task 3 Step 3-4가 검증 절차)

## 1. 빌드

```bash
git pull
source ~/zephyrproject/.venv/bin/activate
cd ~/zephyrproject
west build -p always -b stm32f746g_disco <repo>/fcc_app
west flash --runner openocd     # 반드시 openocd 러너
```

## 2. 화면 확인 (측정 전)

플래시 직후 LCD 좌상단에:

```text
RADAR FCC (HIL)
frames rx : 0
last meas
  t   : --
  rng : --
  az  : --
  dop : --
tracks: 0 (kalman TODO)
```

- 화면 백라이트 켜지고 텍스트 뜨면 LVGL 파이프라인 정상.
- 아무것도 안 뜨면 → `zephyr,display` chosen 노드 / LVGL config / display
  스레드 로그(`[display ] LCD 디바이스 미준비`) 확인.

## 3. HIL 실측 (측정 흐름)

터미널 2개.

```bash
# 터미널 1: Zephyr 콘솔
tio /dev/ttyACM0                      # 115200 8N1

# 터미널 2: PC 시뮬레이터 (배선은 docs/HIL-wiring.md)
cd <repo>/pc_sim
python run_sim.py --serial /dev/ttyUSB0 --scenario single_approach
```

확인:

- LCD `frames rx` 값이 증가.
- LCD `rng`/`az`/`dop`가 콘솔 `[tracking] #N t=... r=... az=...` 최신 로그와 일치.
  - 주의: 콘솔 az는 **rad**, LCD az는 **deg**. `deg = rad * 180 / π`로 대조.
- 시나리오 종료 후 카운터 정지.

## 4. 빌드 실패 시 최우선 의심 지점

이번 구현에서 정적 검토로만 확인된, 실제 빌드에서 처음 드러날 수 있는 리스크:

| 증상 | 원인 후보 | 대응 |
| --- | --- | --- |
| `undeclared 'lv_screen_active'` 또는 `lv_timer_handler` | west manifest가 다른 LVGL 리비전 고정 (이 코드는 Zephyr v4.2 번들 = LVGL v9 기준) | 번들 LVGL 버전 확인. v8이면 `lv_scr_act()`/`lv_task_handler()`로 되돌림. `display.c:36-39` 주석 참고 |
| `undefined 'M_PI'` | libc(picolibc)가 기능 매크로 없이 M_PI 미노출 | 이미 폴백 정의 있음(`display.c:20-22`). 그래도 나면 `_GNU_SOURCE`/`__USE_MISC` 검토 |
| Kconfig `CONFIG_LV_*` 심볼 오류 | Zephyr v4.2에서 심볼명 상이 | `west build` 에러가 정확한 심볼명 제시 → `prj.conf` 수정 |
| 링커 SRAM 오버플로 | `LV_Z_MEM_POOL_SIZE=16384` + `HEAP_MEM_POOL_SIZE=32768` 예산 | 링커 맵 확인. 프레임버퍼는 외부 SDRAM이라 내부 SRAM 흡수 가능해야 정상. `HEAP_MEM_POOL_SIZE`가 이 라벨-only 경로에서 미사용이면 축소/제거(맵 확인 후) |
| `%f`가 빈칸/쓰레기 출력 | 부동소수 printf 미지원 | `CONFIG_CBPRINTF_FP_SUPPORT=y` 확인(prj.conf에 이미 있음) |

## 5. 통과 후

- CLAUDE.md / README 진행 상태의 "LVGL 텍스트 상태판" 항목은 이미 `[x]`로 표기됨 —
  보드 실측까지 끝나면 정직성 유지된 상태.
- 다음 트랙: **Kalman 포팅** (`tracking_thread`의 더미 에코 → 실제 추정, `g_track_table` 갱신).
  이후 트랙 위치가 생기면 상태판을 **PPI 스코프**로 확장.
