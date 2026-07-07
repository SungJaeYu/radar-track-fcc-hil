# HIL 배선 · 통신 구조

STM32(FCC) ↔ PC(시뮬레이터) 폐루프 HIL의 물리 연결과 핀별 신호 정의.

## 1. 큰 그림

PC 하나에 **USB 케이블 2개**가 꽂힌다. 채널이 두 개인 이유:
콘솔(사람이 보는 로그)과 데이터 링크(프레임 스트림)를 분리해야
서로 간섭 없이 디버깅 가능하기 때문.

```c
        ┌────────────────────────────────────────────────────────────┐
        │ PC:  tio /dev/ttyACM0     run_sim.py --serial /dev/ttyUSB0 │
        │       (console / printk)    (data link / frames)           │
        └───────┬──────────────────────────────────┬─────────────────┘
                │ USB (console+flash)               │ USB
                │                                   │
        ┌───────┴───────┐                  ┌────────┴─────────┐
        │  ST-Link VCP  │                  │  USB-UART bridge │
        │  (on board)   │                  │  (FT232RL, 3.3V) │
        └───────┬───────┘                  └────────┬─────────┘
                │ USART1                             │ TTL 3.3V UART
                │                                    │
        ┌───────┴─────────────────────────────────────┴──────┐
        │              STM32F746G-DISCO (FCC)                │
        │   USART1 -> console       USART6 -> HIL data       │
        │                           PG9(RX) / PG14(TX)       │
        └────────────────────────────────────────────────────┘
```

- **채널 A (콘솔·플래시):** DISCO 내장 ST-Link의 가상 COM 포트(VCP) →
  `/dev/ttyACM0`. Zephyr `printk` 로그가 여기로 나온다. `west flash`도 이 USB.
- **채널 B (HIL 데이터):** STM32 USART6를 외부 USB-UART 어댑터로 뽑아 PC에 →
  `/dev/ttyUSB0`. `run_sim.py`가 여기로 MSG_MEAS 프레임을 쏘고 MSG_TRACK을 받는다.

> macOS 포트 이름은 다르다: 어댑터는 `/dev/tty.usbserial-XXXX`,
> ST-Link는 `/dev/tty.usbmodemXXXX`. 위 `ttyACM0`/`ttyUSB0`은 Linux 표기.

## 2. USB-UART 어댑터 선택

아두이노 우노를 브릿지로 쓰지 말 것. 이유:

- 우노 D0/D1은 **5V 로직** → STM32F746 RX(PG9) 3.3V에 직결 시 핀 손상 위험.
  (F7 일부 핀만 5V-tolerant, PG9는 보장 안 됨 → 레벨 시프터 필수)
- 우노 USB-시리얼은 ATmega328에 물려 있어 순수 패스스루가 아니다.
  (328을 리셋 홀드하거나 패스스루 스케치를 굽는 편법 필요)
- UART 1개뿐, 지연·지터 추가.

**권장: FT232RL 기반 USB-TTL 어댑터, 3.3V 설정.**

| 등급 | 칩 | 비고 |
|---|---|---|
| 권장 | **FT232RL** | macOS/Linux 네이티브 드라이버, 3.3V/5V 점퍼 → **3.3V 고정**. 안정성 최고. |
| 대안 | CP2102 | 저렴. OS에 따라 Silicon Labs VCP 드라이버 설치 필요. 3.3V 지원. |
| 비권장 | CH340 | 최저가지만 macOS 드라이버 이슈. HIL 디버깅 중 변수 늘리지 말 것. |

구매 시 체크: **3.3V 로직 선택 가능**(VCCIO 점퍼 또는 3V3 핀), TX/RX/GND 노출.

## 3. 핀 배선

STM32F746G-DISCO의 **아두이노 규격 헤더**(CN 헤더의 Arduino Uno R3 폼팩터)를 쓴다.
별도 아두이노 보드가 아니라 DISCO 보드에 난 헤더다.

| USB-UART 어댑터 | ↔ | STM32 DISCO 핀 | STM32 기능 |
|---|:-:|---|---|
| GND | ── | GND (아두이노 POWER 헤더) | 공통 접지 (필수) |
| TXD (3.3V out) | ──→ | **D0 = PG9** | USART6_RX |
| RXD (3.3V in) | ←── | **D1 = PG14** | USART6_TX |
| VCC / 5V / 3V3 | ✗ | **연결 안 함** | 보드는 ST-Link USB로 자가 전원 |

핵심 규칙:

- **TX↔RX 크로스.** 어댑터 TX → 보드 RX, 어댑터 RX ← 보드 TX. 직결(TX-TX) 하면 무통신.
- **GND는 반드시 공통.** 안 잡으면 프레임 깨지거나 랜덤 CRC 오류.
- **전원선(VCC) 연결 금지.** 보드와 어댑터 둘 다 각자 USB로 급전 →
  전원선 이으면 역급전/충돌. 신호 3선(TX/RX/GND)만.

```text
 FT232RL(3.3V)                STM32F746G-DISCO (Arduino header)
 ┌─────────┐
 │ GND ────┼──────────────── GND
 │ TXD ────┼───────────────▶ D0 (PG9)    USART6_RX
 │ RXD ────┼◀─────────────── D1 (PG14)   USART6_TX
 │ VCC  ✗  │   (not connected)
 └─────────┘
```

## 4. 핀에서 오가는 신호

물리 계층: **UART TTL 3.3V, 115200 8N1** (8데이터·무패리티·1스톱, 흐름제어 없음).
아이들 = High(3.3V), 스타트비트 = Low.

프레임(양쪽 동일):

```text
STX(0x7E) | LEN(1) | TYPE(1) | PAYLOAD(LEN) | CRC16-BE(2)
```

- CRC 범위 = `[LEN, TYPE, PAYLOAD...]` (STX 제외), CRC16-CCITT XModem(poly=0x1021, init=0).
- 페이로드 엔디언 = little-endian.

| 핀 | 방향 | 흐르는 프레임 |
|---|---|---|
| **PG9 (USART6_RX)** | PC → STM32 | `MSG_MEAS`(0x01, 20B): timestamp_ms, range_m, azimuth_rad, elevation_rad, doppler_mps. 필요 시 `MSG_CTRL`(0x03: START/STOP/RESET). |
| **PG14 (USART6_TX)** | STM32 → PC | `MSG_TRACK`(0x02, 21B): timestamp_ms, track_id, x_m, y_m, vx_mps, vy_mps. |
| **USART1 (ST-Link VCP)** | STM32 → PC | UART 아님·프레임 아님. Zephyr `printk` 텍스트 로그 (`[tracking] #N t=...`). 디버깅 전용, 데이터 경로와 분리. |

## 5. 연결·검증 순서

1. 빌드·플래시: `west build ...` → `west flash --runner openocd`.
2. 콘솔: `tio /dev/ttyACM0` (115200 8N1).
3. 어댑터를 3.3V로 설정하고 3선 배선(§3) → PC USB 연결 → `/dev/ttyUSB0` 확인.
4. `python run_sim.py --serial /dev/ttyUSB0 --scenario single_approach`.
5. 콘솔에 `[tracking] #N t=... r=... az=...` → **RX 파이프라인 정상** (PG9 수신 OK).
6. run_sim.py 출력에 `[TRACK] ID0 x=...` → **TX 파이프라인 정상** (PG14 송신 OK).

배선 자가진단:

- 콘솔에 아무 `[tracking]` 로그도 없다 → TX/RX 크로스 뒤바뀜 또는 GND 미연결 의심.
- 로그는 나오는데 CRC 오류 → GND 불량 또는 어댑터 5V 로직(레벨 불일치) 의심.
