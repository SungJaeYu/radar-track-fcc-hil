# radar-track-fcc-hil

STM32 + Zephyr RTOS에서 다중표적 추적을 실시간 처리하고, PC 시뮬레이터와 폐루프 검증하는 Hardware-in-the-Loop(HIL) 프로젝트입니다.
PC가 합성 표적·측정값을 생성하고 STM32가 추적 결과를 반환해 RMSE와 트랙 연속성 같은 지표로 검증하는 구조를 목표로 합니다.

> **개발 방식**  
> 문제 정의, 요구사항, 시스템 구조와 검증 기준은 직접 설계하고 있습니다. 구현 과정에서는 AI 코딩 에이전트를 적극 활용하며, 생성된 코드는 요구사항과 테스트 결과를 기준으로 검토·수정합니다. 이 저장소는 개인 학습·포트폴리오용 합성 시뮬레이션이며 실제 군 운용 데이터나 내부 시스템 정보를 사용하지 않습니다.

```text
           ┌─────────────────────────────────────┐
           │           PC  (Python)              │
           │  RadarSensorModel → encode_frame    │
           │  FrameParser ← TrackPayload rx      │
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

```text
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

```text
fcc_app/src/
    main.c        스레드 생성 진입점
    frame.h/c     바이너리 프레임 코덱 (CRC16, 인코더, FrameParser)
    uart_rx.h/c   UART ISR·링버퍼·FrameParser → meas_msgq
    tracking.h/c  트랙 테이블·Kalman(예정)·TX 에코
    display.h/c   LVGL 텍스트 상태판 (PPI는 예정)

pc_sim/
    protocol.py   프레임 코덱 + FrameParser (Python)
    targets.py    등속 표적 모델 + 센서 측정 모델
    transport.py  MockTransport / SerialTransport
    run_sim.py    시뮬레이터 메인 루프
    test_protocol.py  유닛테스트 (pytest)
```

## 진행 상태

- [x] PC Python 시뮬레이터 (프레이밍, FrameParser, 합성 측정 모델, 유닛테스트)
- [x] STM32 앱 골격 (3-스레드)
- [x] UART 프레이밍 (보드측): frame.h/c (CRC16, encode, FrameParser), USART6 ISR+링버퍼
- [x] STM32 3-스레드 골격: uart_rx.h/c, tracking.h/c, display.h/c (모듈 분리 완료)
- [ ] Kalman 포팅
- [ ] 트랙 관리 (M-of-N, 데이터 연관)
- [x] LVGL 상태판(텍스트): 최신 측정·프레임 카운터 LCD 표시
- [ ] LVGL PPI 디스플레이 (트랙 스코프)
- [ ] HIL 통합 + 정량 검증

## HIL 통신 구조 · 배선

PC에 USB 2개로 콘솔과 데이터 링크를 분리한다. 서로 간섭 없이 디버깅하기 위함.

- **채널 A (콘솔·플래시):** DISCO 내장 ST-Link VCP → `/dev/ttyACM0`.
- **채널 B (HIL 데이터):** STM32 USART6 → 외부 USB-UART 어댑터 → `/dev/ttyUSB0`.

> macOS 포트 이름은 다름: 어댑터 `/dev/tty.usbserial-XXXX`, ST-Link `/dev/tty.usbmodemXXXX`.

### 핀 배선

| USB-UART 어댑터 | ↔    | STM32 DISCO 핀 | STM32 기능 |
| --------------- | :--: | -------------- | ---------- |
| GND             | ──   | GND            | 공통 접지 |
| TXD (3.3V out)  | ──→  | D0 = PG9       | USART6_RX |
| RXD (3.3V in)   | ←──  | D1 = PG14      | USART6_TX |
| VCC / 5V / 3V3  | ✗    | 연결 안 함     | 각자 USB 자가 전원 |

- TX↔RX 크로스 연결.
- GND 공통 연결.
- 보드와 어댑터가 각각 USB로 급전되는 구성에서는 전원선 연결하지 않음.

전체 핀별 신호·구매 가이드는 [docs/HIL-wiring.md](docs/HIL-wiring.md)를 참고합니다.

### 검증 순서

1. 빌드·플래시 후 콘솔 연결
2. 3.3V UART 어댑터 배선 및 데이터 포트 확인
3. `python run_sim.py --serial /dev/ttyUSB0 --scenario single_approach`
4. 보드 로그에서 측정 수신 파이프라인 확인
5. PC 출력에서 트랙 메시지 수신 확인
