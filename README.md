# radar-track-fcc-hil

다중표적 레이더 트래킹을 STM32에서 실시간 처리하는 Hardware-in-the-Loop(HIL) 시스템.
PC가 레이더 환경·측정을 시뮬레이션하고, STM32(FCC 역할)가 트래킹 결과를 PC로 돌려보내
정량 검증(RMSE, 트랙 연속성)하는 폐루프 구조.

```
           ┌─────────────────────────────────────┐
           │           PC  (Python)              │
           │  RadarSensorModel → encode_frame    │
           │  FrameParser ← TrackPayload 수신    │
           └───────────── UART ──────────────────┘
                         │  ↑
              MSG_MEAS   │  │  MSG_TRACK
                         ↓  │
           ┌─────────────────────────────────────┐
           │         STM32F746G-DISCO            │
           │  uart_rx  →  tracking  →  display   │
           │  (ISR/Parser)  (Kalman) (LVGL PPI)  │
           └─────────────────────────────────────┘
```

## 개발 환경

| 항목 | 값 |
|---|---|
| 보드 | STM32F746G-DISCO (Cortex-M7) |
| RTOS | Zephyr v4.2 |
| SDK | `~/zephyr-sdk-1.0.1` |
| west workspace | `~/zephyrproject` |

## 빠른 시작

### 1. 환경 활성화

```bash
source ~/zephyrproject/.venv/bin/activate
```

### 2. 빌드

```bash
cd ~/zephyrproject
west build -p always -b stm32f746g_disco /path/to/radar-track-fcc-hil/fcc_app
```

### 3. 플래시

```bash
west flash --runner openocd
```

### 4. 시리얼 콘솔

```bash
tio /dev/ttyACM0   # 115200 8N1
```

### 5. PC 시뮬레이터

```bash
cd pc_sim

# 보드 없이 PC 단독 테스트
python run_sim.py --mock --scenario single_approach

# 실제 STM32 연결 (USB-UART 어댑터: Arduino D0/D1 → /dev/ttyUSB0)
python run_sim.py --serial /dev/ttyUSB0 --scenario single_approach
```

### 6. 유닛테스트

```bash
cd pc_sim && python -m pytest test_protocol.py -v
```

## UART 프레임 포맷

```
STX(1) | LEN(1) | TYPE(1) | PAYLOAD(LEN) | CRC16-BE(2)
```

| TYPE | 방향 | 페이로드 |
|---|---|---|
| 0x01 MSG_MEAS | PC→STM32 | timestamp_ms, range_m, azimuth_rad, elevation_rad, doppler_mps |
| 0x02 MSG_TRACK | STM32→PC | timestamp_ms, track_id, x_m, y_m, vx_mps, vy_mps |
| 0x03 MSG_CTRL | 양방향 | cmd (START=1, STOP=2, RESET=3) |

## 파일 구조

```
fcc_app/src/
    main.c        스레드 생성 진입점
    frame.h/c     바이너리 프레임 코덱 (CRC16, 인코더, FrameParser)
    uart_rx.h/c   UART ISR·링버퍼·FrameParser → meas_msgq
    tracking.h/c  트랙 테이블·Kalman(예정)·TX 에코
    display.h/c   LVGL PPI 디스플레이(예정)

pc_sim/
    protocol.py   프레임 코덱 + FrameParser (Python)
    targets.py    등속 표적 모델 + 레이더 센서 모델
    transport.py  MockTransport / SerialTransport
    run_sim.py    시뮬레이터 메인 루프
    test_protocol.py  유닛테스트 (pytest)
```

## 진행 상태

- [x] PC Python 시뮬레이터 (프레이밍, FrameParser, RadarSensorModel, 유닛테스트)
- [x] STM32 앱 골격 (3-스레드)
- [x] UART 프레이밍 (보드측): frame.h/c, uart_rx.h/c, tracking.h/c, display.h/c
- [ ] Kalman 포팅 ← 다음 작업
- [ ] 트랙 관리 (M-of-N, 데이터 연관)
- [ ] LVGL PPI 디스플레이
- [ ] HIL 통합 + 정량 검증

## HIL 통합 검증 방법

1. 빌드·플래시 후 `tio /dev/ttyACM0` 으로 Zephyr 콘솔 연결
2. USB-UART 어댑터로 Arduino D0(RX)/D1(TX) 연결 → `/dev/ttyUSB0`
3. `python run_sim.py --serial /dev/ttyUSB0 --scenario single_approach`
4. 콘솔에 `[tracking] #N t=... r=... az=...` 로그 → RX 파이프라인 정상
5. run_sim.py 출력에 `[TRACK] ID0 x=...` → TX 파이프라인 정상
