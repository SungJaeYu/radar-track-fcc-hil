# radar-track-fcc-hil

다중표적 레이더 트래킹을 STM32에서 실시간 처리하는 Hardware-in-the-Loop(HIL) 시스템.
PC가 레이더 환경·측정을 시뮬레이션하고, STM32(FCC 역할)가 트래킹 결과를 PC로 돌려보내
정량 검증(RMSE, 트랙 연속성)하는 폐루프 구조.

```
           ┌─────────────────────────────────────┐
           │           PC  (Python)              │
           │  RadarSensorModel → encode_frame    │
           │  FrameParser ← TrackPayload 수신     │
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

| 항목           | 값                           |
| -------------- | ---------------------------- |
| 보드           | STM32F746G-DISCO (Cortex-M7) |
| RTOS           | Zephyr v4.2                  |
| SDK            | `~/zephyr-sdk-1.0.1`         |
| west workspace | `~/zephyrproject`            |

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

# 실제 STM32 연결 (USB-UART 어댑터 → /dev/ttyUSB0, 배선은 docs/HIL-wiring.md)
python run_sim.py --serial /dev/ttyUSB0 --scenario single_approach
```

### 6. 유닛테스트

```bash
cd pc_sim && python -m pytest test_protocol.py -v
```

## UART 프레임 포맷

```
STX(0x7E) | LEN(1) | TYPE(1) | PAYLOAD(LEN) | CRC16-BE(2)
```

- CRC 범위 = `[LEN, TYPE, PAYLOAD...]` (STX 제외), CRC16-CCITT XModem (poly=0x1021, init=0).
- 페이로드 엔디언 = little-endian. 물리 계층 = UART TTL 3.3V, 115200 8N1, 흐름제어 없음.

| TYPE           | 방향     | 페이로드                                                       |
| -------------- | -------- | -------------------------------------------------------------- |
| 0x01 MSG_MEAS  | PC→STM32 | timestamp_ms, range_m, azimuth_rad, elevation_rad, doppler_mps |
| 0x02 MSG_TRACK | STM32→PC | timestamp_ms, track_id, x_m, y_m, vx_mps, vy_mps               |
| 0x03 MSG_CTRL  | 양방향   | cmd (START=1, STOP=2, RESET=3)                                 |

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
- [x] UART 프레이밍 (보드측): frame.h/c (CRC16, encode, FrameParser), USART6 ISR+링버퍼
- [x] STM32 3-스레드 골격: uart_rx.h/c, tracking.h/c, display.h/c (모듈 분리 완료)
- [ ] Kalman 포팅 ← 다음 작업
- [ ] 트랙 관리 (M-of-N, 데이터 연관)
- [ ] LVGL PPI 디스플레이
- [ ] HIL 통합 + 정량 검증

## HIL 통신 구조 · 배선

PC에 USB 2개로 콘솔과 데이터 링크를 분리한다. 서로 간섭 없이 디버깅하기 위함.

- **채널 A (콘솔·플래시):** DISCO 내장 ST-Link VCP → `/dev/ttyACM0`.
  Zephyr `printk` 로그·`west flash` 경로. (USART1)
- **채널 B (HIL 데이터):** STM32 USART6 → 외부 USB-UART 어댑터 → `/dev/ttyUSB0`.
  `run_sim.py`가 MSG_MEAS 송신·MSG_TRACK 수신. (PG9/PG14)

> macOS 포트 이름은 다름: 어댑터 `/dev/tty.usbserial-XXXX`, ST-Link `/dev/tty.usbmodemXXXX`.
> 위 `ttyACM0`/`ttyUSB0`은 Linux 표기.

### 핀 배선 (3선만, 전원선 금지)

STM32F746G-DISCO의 아두이노 규격 헤더 사용.

| USB-UART 어댑터 | ↔    | STM32 DISCO 핀 | STM32 기능 |
| --------------- | :--: | -------------- | ---------- |
| GND             | ──   | GND (POWER 헤더) | 공통 접지 (필수) |
| TXD (3.3V out)  | ──→  | **D0 = PG9**   | USART6_RX  |
| RXD (3.3V in)   | ←──  | **D1 = PG14**  | USART6_TX  |
| VCC / 5V / 3V3  | ✗    | **연결 안 함**   | 각자 USB 자가 전원 |

- **TX↔RX 크로스.** 직결(TX-TX) 하면 무통신.
- **GND 반드시 공통.** 안 잡으면 프레임 깨짐·랜덤 CRC 오류.
- **전원선(VCC) 연결 금지.** 보드·어댑터 각자 USB 급전 → 이으면 역급전/충돌.

### 어댑터 선택

**아두이노 우노 브릿지 금지** — D0/D1이 5V 로직이라 PG9(3.3V) 손상 위험, 게다가 순수 패스스루 아님.

| 등급   | 칩          | 비고 |
| ------ | ----------- | ---- |
| 권장   | **FT232RL** | macOS/Linux 네이티브 드라이버, 3.3V/5V 점퍼 → **3.3V 고정**. 안정성 최고. |
| 대안   | CP2102      | 저렴. Silicon Labs VCP 드라이버 필요할 수 있음. 3.3V 지원. |
| 비권장 | CH340       | 최저가지만 macOS 드라이버 이슈. |

구매 체크: **3.3V 로직 선택 가능**(VCCIO 점퍼/3V3 핀), TX/RX/GND 노출.

전체 핀별 신호·구매 가이드는 **[docs/HIL-wiring.md](docs/HIL-wiring.md)**.

### 검증 순서

1. 빌드·플래시 후 `tio /dev/ttyACM0` 으로 Zephyr 콘솔 연결
2. 어댑터 3.3V 설정 + 3선 배선 → PC USB → `/dev/ttyUSB0` 확인
3. `python run_sim.py --serial /dev/ttyUSB0 --scenario single_approach`
4. 콘솔에 `[tracking] #N t=... r=... az=...` 로그 → RX 파이프라인 정상 (PG9 OK)
5. run_sim.py 출력에 `[TRACK] ID0 x=...` → TX 파이프라인 정상 (PG14 OK)

배선 자가진단:

- `[tracking]` 로그 전무 → TX/RX 크로스 뒤바뀜 또는 GND 미연결 의심.
- 로그는 나오는데 CRC 오류 → GND 불량 또는 어댑터 5V 로직(레벨 불일치) 의심.
