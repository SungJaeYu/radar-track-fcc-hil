# radar-track-fcc-hil 코드 전체 설명서

> 대상 독자: 컴퓨터공학 전공 지식 보유자 (OS, 네트워크, 자료구조 수준)
> 임베디드/레이더 배경지식 없어도 읽을 수 있도록 씀

---

## 0. 이게 뭘 만드는 건가

레이더가 공중 표적(비행기, 미사일 등)을 추적하는 시스템을 만든다.
핵심은 **추적 알고리즘을 STM32 마이크로컨트롤러에 올리는 것**이다.

문제: 알고리즘이 잘 동작하는지 어떻게 검증하나?
→ **HIL(Hardware-in-the-Loop)**: PC가 가짜 레이더 환경을 만들어주고,
STM32가 그걸 받아서 트래킹한 결과를 다시 PC로 보내면,
PC가 "정답(ground truth)"과 비교해서 오차를 측정한다.

```
┌─────────────────────────────────────────┐
│  PC (Python)                            │
│  "target here" -> generate measurement  │
│  send to STM32 over UART                │
│                                         │
│  receive track result from STM32        │
│  compute error (RMSE) vs ground truth   │
└─────────────────────────────────────────┘
        │  UART (USB-to-Serial), 115200 bps
        ▼
┌──────────────────────────────────────┐
│  STM32F746G-DISCO                    │
│  uart_rx -> tracking -> display      │
│  (ISR/parser)  (Kalman)  (LVGL PPI)  │
└──────────────────────────────────────┘
```

**현재 구현 단계**: 통신 토대까지 완성, 추적 알고리즘은 아직 없음.

- STM32: 3개 스레드 + **UART 프레이밍 실제 동작**(ISR·링버퍼·CRC·파서). tracking은 더미 에코.
- PC: 시뮬레이터 + 통신 프레임 코덱 완성, 유닛테스트 29개.

---

## 1. 전체 파일 구조

```
radar-track-fcc-hil/
│
├── fcc_app/                  ← STM32에 올라가는 C 프로그램 (Zephyr 앱)
│   ├── CMakeLists.txt        ← 빌드 설정
│   ├── prj.conf              ← RTOS 기능 켜기/끄기 (Kconfig)
│   ├── boards/
│   │   └── stm32f746g_disco.overlay  ← USART6 핀 설정 (devicetree)
│   └── src/
│       ├── main.c            ← 진입점: 스레드 3개 생성만
│       ├── frame.h / frame.c ← 바이너리 프레임 코덱 (CRC16·인코더·FrameParser)
│       ├── uart_rx.h / .c    ← UART ISR·링버퍼·파싱 → meas_msgq
│       ├── tracking.h / .c   ← 트랙 테이블·(칼만 예정)·TX 에코
│       └── display.h / .c    ← (LVGL PPI 예정) 현재는 활성 트랙 수 로그
│
└── pc_sim/                   ← PC에서 돌아가는 Python 시뮬레이터
    ├── protocol.py           ← 바이너리 프레임 코덱 + FrameParser
    ├── targets.py            ← 표적 운동 모델 + 레이더 센서 모델
    ├── transport.py          ← UART 추상화 (Mock / 실제 Serial)
    ├── run_sim.py            ← 시뮬레이터 메인 루프
    └── test_protocol.py      ← 유닛테스트 (pytest, 29개)
```

> **핵심 통찰**: PC의 `protocol.py`와 STM32의 `frame.c`는 **같은 규약의 두 언어 구현**이다.
> Python이 먼저 만들어지고 유닛테스트로 검증되어 **"정답 기준(reference)"**이 되고,
> C가 그걸 바이트 단위로 똑같이 따라간다.

---

## 2. STM32 측 — `fcc_app/`

### 2-1. 배경: Zephyr RTOS란

STM32F746은 일반 PC가 아니다. RAM이 ~320KB, CPU가 216MHz. 여기서 OS 없이
코딩하면 `while(1)` 루프 하나만 돌 수 있다.

**Zephyr**는 이런 작은 칩용 RTOS다. Linux처럼 스레드, 뮤텍스, 메시지큐, 세마포어,
링버퍼를 제공하는데 커널 크기는 수십 KB에 불과하다.

Zephyr 앱 빌드에 필요한 파일:

- `CMakeLists.txt`: "이 소스파일로 앱 빌드해줘" → cmake/ninja가 읽음
- `prj.conf`: "이 RTOS 기능 켜줘" → Kconfig 시스템이 읽음
- `boards/*.overlay`: "이 핀을 이 주변장치로 써줘" → devicetree
- `src/*.c`: 실제 코드

### 2-2. `CMakeLists.txt`

```cmake
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(fcc_app)

target_sources(app PRIVATE
    src/main.c
    src/frame.c
    src/uart_rx.c
    src/tracking.c
    src/display.c
)
```

`find_package(Zephyr)`가 컴파일러·링커 스크립트·HAL을 전부 설정한다.
`target_sources`는 다섯 개 소스를 빌드 대상에 추가. (헤더는 자동 인클루드.)

### 2-3. `prj.conf` (Kconfig)

```
CONFIG_PRINTK=y                 # printk() 콘솔 출력
CONFIG_LOG=y                    # 로그 서브시스템
CONFIG_LOG_DEFAULT_LEVEL=3      # info 레벨
CONFIG_THREAD_NAME=y            # 스레드 이름 (디버깅용)
CONFIG_THREAD_STACK_INFO=y      # 스택 사용량 통계
CONFIG_UART_INTERRUPT_DRIVEN=y  # UART 인터럽트 방식 수신 (ISR)
CONFIG_RING_BUFFER=y            # ISR→스레드 바이트 전달용 링버퍼
```

`=y`로 켠 기능만 바이너리에 들어간다(크기 절약). 마지막 두 줄이 uart_rx.c의
ISR+링버퍼 패턴을 가능케 한다.

### 2-4. `boards/stm32f746g_disco.overlay` (devicetree)

```dts
&usart6 {
    pinctrl-0 = <&usart6_tx_pg14 &usart6_rx_pg9>;
    pinctrl-names = "default";
    current-speed = <115200>;
    status = "okay";
};
```

보드의 **USART6**를 켜고 Arduino 헤더 **D1(PG14)=TX, D0(PG9)=RX**에 매핑.
여기에 USB-UART 어댑터를 꽂으면 PC에서 `/dev/ttyUSB0`로 보인다.

> 콘솔용 USART1(ST-Link, `/dev/ttyACM0`)은 안 건드린다 → printk 로그는 그대로.
> **데이터선(USART6)과 로그선(USART1)을 물리적으로 분리**한 설계.

### 2-5. `src/main.c` — 진입점

역할은 단 하나: **스레드 3개의 스택·제어블록을 잡고 생성**한다. 로직은 각 모듈로 분리됨.

```c
#define STACK_UART_RX  2048
#define STACK_TRACKING 4096      // 칼만 행렬 연산 대비 가장 큼
#define STACK_DISPLAY  2048

K_THREAD_STACK_DEFINE(uart_rx_stack,  STACK_UART_RX);   // 스택 정적 할당
K_THREAD_STACK_DEFINE(tracking_stack, STACK_TRACKING);
K_THREAD_STACK_DEFINE(display_stack,  STACK_DISPLAY);

static struct k_thread uart_rx_td, tracking_td, display_td;  // 스레드 제어블록

int main(void)
{
    printk("=== FCC HIL booting ===\n");

    k_thread_create(&uart_rx_td, uart_rx_stack, K_THREAD_STACK_SIZEOF(uart_rx_stack),
                    uart_rx_thread, NULL, NULL, NULL, 5, 0, K_NO_WAIT);  // 우선순위 5
    k_thread_name_set(&uart_rx_td, "uart_rx");

    k_thread_create(&tracking_td, ..., tracking_thread, ..., 6, 0, K_NO_WAIT);
    k_thread_create(&display_td,  ..., display_thread,  ..., 7, 0, K_NO_WAIT);
    return 0;
}
```

#### 스레드가 왜 3개인가

| 스레드     | 하는 일                 | 우선순위 | 지금 상태                  |
| ---------- | ----------------------- | -------- | -------------------------- |
| `uart_rx`  | PC에서 측정값 받기·파싱 | 5 (높음) | **실동작** (ISR·파서→큐)   |
| `tracking` | 칼만 필터로 위치 계산   | 6        | 더미 에코 (range→x 되돌림) |
| `display`  | LCD에 트랙 화면 그리기  | 7 (낮음) | 활성 트랙 수 로그          |

셋을 `while(1)` 하나로 돌리면 UART 수신 중에 화면이 멈추고, 화면 그리는 중에
데이터를 놓친다. 스레드로 분리하면 Zephyr 스케줄러가 우선순위대로 번갈아 실행한다.
수신을 놓치면 안 되니 `uart_rx`가 가장 높은 우선순위.

#### `k_thread_create` 인자

OS 수업의 `pthread_create`와 개념 동일. 차이: **스택을 직접 줘야 한다**(heap이 없거나
극히 제한적이라서). 인자 순서 = TCB 포인터, 스택주소, 스택크기, 진입함수, 인자 3개,
우선순위(작을수록 높음), 플래그, 시작시점(`K_NO_WAIT`=즉시).

`main()`이 `return 0`해도 세 스레드는 계속 돈다(main도 하나의 스레드일 뿐).

---

### 2-6. `frame.h` / `frame.c` — 바이너리 프레임 코덱

Zephyr에 의존하지 않는 **순수 C**. PC `protocol.py`를 그대로 포팅한 것.

#### 왜 바이너리인가

문자열 `"range=1234.5\n"` 대신 바이너리를 쓰는 이유:

- **크기**: float을 ASCII로 8~12바이트 vs 바이너리 4바이트
- **속도**: STM32에서 `atof()` 파싱은 느리고 오류 가능성 높음
- **오류 감지**: 바이너리+CRC로 데이터 깨짐 검출

#### 프레임 구조

```
byte:    0      1      2      3 ... 3+LEN-1  3+LEN  4+LEN
        ┌──────┬──────┬──────┬───────────┬───────┬───────┐
        │ STX  │ LEN  │ TYPE │  PAYLOAD  │ CRC_H │ CRC_L │
        │ 0x7E │  N   │      │  N bytes  │       │       │
        └──────┴──────┴──────┴───────────┴───────┴───────┘
        └────────── CRC range ──────────┘  (STX excluded)
```

총 프레임 = `5 + LEN` 바이트. CRC16-CCITT(XModem), big-endian 전송.

#### 메시지 타입별 페이로드 (packed 구조체)

```c
typedef struct __attribute__((packed)) {
    uint32_t timestamp_ms; float range_m, azimuth_rad, elevation_rad, doppler_mps;
} FccMeasPayload;   /* 20바이트, MSG_MEAS=0x01, PC→STM32 */

typedef struct __attribute__((packed)) {
    uint32_t timestamp_ms; uint8_t track_id; float x_m, y_m, vx_mps, vy_mps;
} FccTrackPayload;  /* 21바이트, MSG_TRACK=0x02, STM32→PC */

typedef struct __attribute__((packed)) {
    uint8_t cmd;
} FccCtrlPayload;   /* 1바이트, MSG_CTRL=0x03, 양방향 (START/STOP/RESET) */
```

모두 **little-endian**(ARM Cortex-M 기본). Python `struct.pack("<...")`와 1:1 대응.

> **`__attribute__((packed))`가 핵심**: C 컴파일러는 정렬을 위해 멤버 사이 패딩을 넣는다.
> 예: `FccTrackPayload`의 `uint32_t` 다음 `uint8_t`가 오면 그 뒤에 3바이트 패딩이 생겨
> 24바이트가 돼버린다. packed가 패딩을 막아 21바이트로 맞춘다.
> Python `<` 포맷은 패딩이 없으니 양쪽 레이아웃을 일치시키려면 필수.

`frame.c`는 컴파일 타임에 이를 검증한다:

```c
BUILD_ASSERT(sizeof(FccMeasPayload)  == 20, "FccMeasPayload 크기 불일치");
BUILD_ASSERT(sizeof(FccTrackPayload) == 21, "FccTrackPayload 크기 불일치");
BUILD_ASSERT(sizeof(FccCtrlPayload)  ==  1, "FccCtrlPayload 크기 불일치");
```

#### `fcc_crc16()`

```c
uint16_t fcc_crc16(const uint8_t *data, uint16_t len) {
    uint16_t crc = 0x0000U;
    for (uint16_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int j = 0; j < 8; j++)
            crc = (crc & 0x8000U) ? (uint16_t)((crc << 1) ^ 0x1021U)
                                  : (uint16_t)(crc << 1);
    }
    return crc;
}
```

CRC(Cyclic Redundancy Check)는 데이터 오류 감지 체크섬. `0x1021`은 CRC16-CCITT 생성 다항식.
검증 닻: `fcc_crc16("123456789") == 0x31C3`. PC `crc16_ccitt()`와 동일 동작.

#### `fcc_encode_frame()`

```c
uint16_t fcc_encode_frame(uint8_t msg_type, const uint8_t *payload,
                          uint8_t payload_len, uint8_t *buf, uint16_t buf_size)
{
    uint16_t total = 5U + payload_len;
    if (buf_size < total) return 0;          // 버퍼 부족 → 실패

    uint8_t crc_in[2U + FCC_MAX_PAYLOAD];
    crc_in[0] = payload_len; crc_in[1] = msg_type;   // CRC 입력 = [LEN, TYPE, PAYLOAD]
    memcpy(&crc_in[2], payload, payload_len);
    uint16_t crc = fcc_crc16(crc_in, 2U + payload_len);

    buf[0] = FCC_STX; buf[1] = payload_len; buf[2] = msg_type;
    memcpy(&buf[3], payload, payload_len);
    buf[3U + payload_len]      = (uint8_t)(crc >> 8);    // CRC big-endian
    buf[3U + payload_len + 1U] = (uint8_t)(crc & 0xFFU);
    return total;                            // 프레임 총 바이트 수
}
```

PC 버전과 달리 **호출자가 출력 버퍼를 준다**(임베디드는 동적할당 회피). 버퍼 작으면 0 반환.

#### `FrameParser` — 수신 상태머신 (핵심)

UART는 경계 없는 바이트 스트림. "어디부터 어디까지가 한 프레임인지"를 바이트 하나씩
먹으며 알아내는 6-상태 머신.

```
         b==STX          LEN<=64            always      buf full
WAIT_STX ──▶ WAIT_LEN ──────▶ WAIT_TYPE ──▶ WAIT_PAYLOAD ──┐
   ▲  ▲      │ LEN>64                                      │ (LEN==0: skip)
   │  └────────┘ (garbage, resync)                         ▼
                                             WAIT_CRC_H ──▶ WAIT_CRC_L
   └──── CRC match: emit frame / mismatch: drop ◀────────┘
         (either way -> back to WAIT_STX)
```

```c
bool frame_parser_feed(FrameParser *p, uint8_t b, FccFrame *out)
{
    switch (p->state) {
    case FP_WAIT_STX:
        if (b == FCC_STX) p->state = FP_WAIT_LEN;
        break;
    case FP_WAIT_LEN:
        if (b > FCC_MAX_PAYLOAD) p->state = FP_WAIT_STX;  // 가비지 재동기화
        else { p->len = b; p->state = FP_WAIT_TYPE; }
        break;
    case FP_WAIT_TYPE:
        p->msg_type = b; p->buf_idx = 0;
        p->state = (p->len > 0U) ? FP_WAIT_PAYLOAD : FP_WAIT_CRC_H;
        break;
    case FP_WAIT_PAYLOAD:
        p->buf[p->buf_idx++] = b;
        if (p->buf_idx == p->len) p->state = FP_WAIT_CRC_H;
        break;
    case FP_WAIT_CRC_H:
        p->crc_h = b; p->state = FP_WAIT_CRC_L;
        break;
    case FP_WAIT_CRC_L: {
        uint16_t received = ((uint16_t)p->crc_h << 8) | b;
        uint8_t crc_in[2U + FCC_MAX_PAYLOAD];
        crc_in[0] = p->len; crc_in[1] = p->msg_type;
        memcpy(&crc_in[2], p->buf, p->buf_idx);
        uint16_t expected = fcc_crc16(crc_in, 2U + p->buf_idx);
        p->state = FP_WAIT_STX;                 // 항상 STX로 복귀
        if (received == expected) {             // CRC OK → 프레임 방출
            if (out) { out->msg_type = p->msg_type;
                       out->payload_len = p->buf_idx;
                       memcpy(out->payload, p->buf, p->buf_idx); }
            return true;
        }
        break;                                  // CRC 불일치 → 버림
    }
    }
    return false;
}
```

**`FCC_MAX_PAYLOAD = 64` 가드의 이유**: 가비지 안에 우연히 `0x7E`가 있으면 파서가
STX로 착각하고, 다음 바이트가 `0xAB(=171)`면 "171바이트 페이로드"를 기다리며 진짜
프레임을 삼킨다. `LEN > 64`면 "정상 프레임 아님"으로 즉시 재동기화. 우리 최대 페이로드는
21바이트라 64는 충분한 여유. C는 한 번에 바이트 1개를 먹고 완성 시 `out` 채우고 `true` 반환.

---

### 2-7. `uart_rx.h` / `uart_rx.c` — UART 수신 모듈

데이터 흐름: **USART6 ISR → 링버퍼 → FrameParser → meas_msgq**

#### 공개 인터페이스 (헤더)

```c
extern const struct device *g_hil_uart;   // USART6 장치 (tracking.c가 TX에 재사용)
extern struct k_msgq        meas_msgq;     // 측정값 큐 (tracking.c가 꺼냄)
void uart_rx_thread(void *p1, void *p2, void *p3);
```

#### 모듈 전역 객체

```c
const struct device *g_hil_uart = DEVICE_DT_GET(DT_NODELABEL(usart6));  // overlay와 연결

#define HIL_RX_BUF_SIZE 256U
RING_BUF_DECLARE(g_uart_rx_ring, HIL_RX_BUF_SIZE);   // ISR↔스레드 바이트 FIFO
static K_SEM_DEFINE(g_uart_rx_sem, 0, 1);            // ISR이 스레드를 깨우는 신호
static volatile uint32_t g_rx_overflow_cnt;          // 링버퍼 넘침 횟수 (진단)

K_MSGQ_DEFINE(meas_msgq, sizeof(FccMeasPayload), 8, 4);  // 측정 큐: 8슬롯
```

OS 수업의 **Bounded Buffer** 그대로다. 큐 슬롯 8개라 tracking이 밀려도 8개까지 버퍼링.

#### `hil_uart_isr` — 인터럽트 핸들러

```c
static void hil_uart_isr(const struct device *dev, void *user_data)
{
    uint8_t byte; bool got_data = false;
    uart_irq_update(dev);                        // 인터럽트 상태 갱신 (Zephyr 4.x 필수)
    while (uart_irq_is_pending(dev)) {
        if (!uart_irq_rx_ready(dev)) break;
        while (uart_fifo_read(dev, &byte, 1) > 0) {     // 하드웨어 FIFO 비우기
            if (ring_buf_put(&g_uart_rx_ring, &byte, 1) == 0)
                g_rx_overflow_cnt++;             // 링버퍼 꽉참 → 카운트만
            got_data = true;
        }
    }
    if (got_data) k_sem_give(&g_uart_rx_sem);    // 스레드 깨우기
}
```

**ISR 설계 원칙**: 짧고 가볍게. 바이트를 FIFO에서 꺼내 링버퍼에 던지고 세마포어만 준다.
**`printk` 금지**(ISR 컨텍스트) → 넘침을 로그 대신 카운터로 센다. 파싱·CRC 같은 무거운
일은 일반 스레드로 미룬다. 이 "ISR은 적재만, 처리는 스레드가" 패턴이 임베디드 표준.

#### `uart_rx_thread`

```c
void uart_rx_thread(void *p1, void *p2, void *p3)
{
    FrameParser parser; FccFrame frame;
    frame_parser_init(&parser);

    if (!device_is_ready(g_hil_uart)) {                  // UART 없으면 HIL 비활성
        printk("[uart_rx ] USART6 준비 안됨 - HIL 비활성\n");
        while (1) k_sleep(K_SECONDS(10));
    }
    uart_irq_callback_set(g_hil_uart, hil_uart_isr);     // ISR 등록
    uart_irq_rx_enable(g_hil_uart);                      // 수신 인터럽트 켬

    while (1) {
        k_sem_take(&g_uart_rx_sem, K_FOREVER);           // 바이트 올 때까지 잠
        uint8_t byte;
        while (ring_buf_get(&g_uart_rx_ring, &byte, 1) == 1) {
            if (!frame_parser_feed(&parser, byte, &frame)) continue;

            if (frame.msg_type == FCC_MSG_MEAS &&
                frame.payload_len == sizeof(FccMeasPayload)) {
                FccMeasPayload msg = *(const FccMeasPayload *)frame.payload;
                if (k_msgq_put(&meas_msgq, &msg, K_NO_WAIT) != 0) {
                    k_msgq_purge(&meas_msgq);            // 큐 포화 → 비우고 진단
                    printk("[uart_rx ] meas_msgq 포화, 퍼지 (오버플로:%u)\n",
                           g_rx_overflow_cnt);
                    g_rx_overflow_cnt = 0;
                }
            } else if (frame.msg_type == FCC_MSG_CTRL &&
                       frame.payload_len == sizeof(FccCtrlPayload)) {
                const FccCtrlPayload *cp = (const FccCtrlPayload *)frame.payload;
                if (cp->cmd == FCC_CTRL_RESET) {         // RESET: 파서·큐 초기화
                    frame_parser_reset(&parser);
                    k_msgq_purge(&meas_msgq);
                    printk("[uart_rx ] CTRL_RESET\n");
                }
            }
        }
    }
}
```

세마포어로 잠들었다가 ISR이 깨우면 링버퍼를 **싹 비우며** 바이트마다 파서에 공급.
완성 프레임이 MEAS면 큐에 넣고(포화 시 purge로 묵은 데이터 버림 — 실시간 시스템은 최신이 중요),
CTRL+RESET이면 파서·큐 초기화. `payload_len == sizeof(...)` 길이 검증으로 잘못된 프레임 차단.
`K_NO_WAIT`로 put → 큐가 차도 수신 스레드가 멈추지 않게.

---

### 2-8. `tracking.h` / `tracking.c` — 트래킹 모듈

#### 트랙 테이블 (헤더)

```c
#define MAX_TRACKS 8
struct track {
    bool active; uint8_t id;
    /* TODO: 칼만 상태벡터(x,y,vx,vy), 공분산, M-of-N 카운터 */
};
struct track_table { struct track tracks[MAX_TRACKS]; };

extern struct track_table g_track_table;   // tracking이 쓰고 display가 읽는 공유 데이터
extern struct k_mutex     g_track_mutex;   // 그걸 보호하는 뮤텍스
```

`tracking`이 쓰고 `display`가 읽는 공유 데이터 → **뮤텍스로 임계구역 보호**.
`struct track`은 아직 골격(active/id). 칼만 상태가 들어갈 자리가 주석에 표시됨.

#### `fcc_send_track` — 트랙 TX

```c
static void fcc_send_track(const FccTrackPayload *tp)
{
    /* TODO: uart_poll_out은 바이트당 busy-wait (~1.8ms/프레임 @115200).
     * Kalman 포팅 후 지터 문제 시 TX 큐 + 별도 스레드로 분리할 것. */
    uint8_t  buf[5U + sizeof(FccTrackPayload)];
    uint16_t n = fcc_encode_frame(FCC_MSG_TRACK, (const uint8_t *)tp,
                                  sizeof(*tp), buf, sizeof(buf));
    for (uint16_t i = 0; i < n; i++)
        uart_poll_out(g_hil_uart, buf[i]);   // 바이트마다 busy-wait 전송
}
```

`uart_poll_out`은 **폴링 방식**: 송신 레지스터 빌 때까지 CPU가 기다렸다 한 바이트 쓴다.
프레임(26바이트)에 ~1.8ms를 CPU가 잡아먹는다. 골격 단계엔 단순해서 OK, 칼만이 들어가
타이밍이 빡빡해지면 TX 큐+전용 스레드로 바꿔야 한다고 주석이 예고(정직한 TODO).

#### `tracking_thread`

```c
void tracking_thread(void *p1, void *p2, void *p3)
{
    const bool tx_ready = device_is_ready(g_hil_uart);
    FccMeasPayload mp; uint32_t cnt = 0U;

    while (1) {
        k_msgq_get(&meas_msgq, &mp, K_FOREVER);       // 측정 올 때까지 블록

        printk("[tracking] #%u t=%u r=%.1f az=%.3f dop=%.1f\n",
               cnt, mp.timestamp_ms, (double)mp.range_m,
               (double)mp.azimuth_rad, (double)mp.doppler_mps);

        /* TODO: 칼만 업데이트 + 트랙 관리 후 g_track_table 갱신.
         * 현재 테이블 미갱신 → display는 항상 "활성 트랙: 0".
         * 임시: range를 x에 넣은 더미 에코 */
        if (tx_ready) {
            FccTrackPayload tp = { .timestamp_ms = mp.timestamp_ms, .track_id = 0U,
                .x_m = mp.range_m, .y_m = 0.0f, .vx_mps = 0.0f, .vy_mps = 0.0f };
            fcc_send_track(&tp);
        }
        cnt++;
    }
}
```

현재 동작: ① 큐에서 측정 꺼냄 → ② 콘솔 로그(=**RX 파이프라인 증거**) → ③ range를 x에 넣은
**더미 에코** TX(=**TX 파이프라인 증거**). 즉 트래킹은 안 하고 되돌려보내기만. 그래도
**RX→처리→TX 왕복 전체가 동작함**을 증명한다 — 칼만 얹기 전 통신 토대 검증("사다리 첫 칸").

> 정직성 주석: `g_track_table`을 안 채우므로 display는 늘 "활성 트랙: 0". 숨기지 않고 명시.
> `printk("%.1f", (double)x)`: C 가변인자에서 float→double 승격 경고 회피용 캐스팅.

---

### 2-9. `display.h` / `display.c` — 디스플레이 모듈

(앞으로 LVGL로 PPI 레이더 화면 그릴) 자리. 현재는 활성 트랙 수만 로그.

```c
void display_thread(void *p1, void *p2, void *p3)
{
    while (1) {
        k_mutex_lock(&g_track_mutex, K_FOREVER);     // 공유 테이블 잠금
        uint8_t active = 0U;
        for (int i = 0; i < MAX_TRACKS; i++)
            if (g_track_table.tracks[i].active) active++;
        k_mutex_unlock(&g_track_mutex);              // 즉시 해제

        printk("[display ] 활성 트랙: %u\n", active);
        k_sleep(K_MSEC(2000));                       // 2초 주기
    }
}
```

**뮤텍스 패턴**: 공유 테이블 읽기 전 lock, 다 읽으면 즉시 unlock. 임계구역 **최소화**
(카운트만 세고 바로 풀기). LVGL 렌더링 같은 무거운 일은 잠금 밖에서 해야 tracking을 안 막는다.
tracking이 테이블을 아직 안 채우니 출력은 늘 "활성 트랙: 0".

---

### 2-10. 모듈 간 관계 요약

```
┌──────────────────────────────────────────────┐
│  frame.h/c  (pure C, no Zephyr dep)          │
│  fcc_crc16 . fcc_encode_frame . FrameParser  │
└──────────────────────────────────────────────┘
   decode(RX) ▲                     ▲ encode(TX)
              │                     │
 USART6 ISR   │                     │
 → ring_buf → uart_rx_thread →[meas_msgq]→ tracking_thread → USART6(poll_out)
                                       │ (write)
                       g_track_table ← (read) display_thread
                       [protected by g_track_mutex]
 main.c: only creates the three threads above.
```

frame.h/c는 RTOS 비의존 순수 C라 PC 테스트·재사용 가능. 스레드 결합은 **메시지큐**와
**뮤텍스로 보호된 테이블** 둘뿐 — 느슨한 결합.

---

## 3. PC 측 — `pc_sim/`

### 3-1. 의존 관계

```
run_sim.py ──┬─▶ protocol.py    (프레임 인코딩/디코딩)
             ├─▶ targets.py     (표적 + 센서 모델) ─▶ protocol.py
             └─▶ transport.py   (보내기/받기)
test_protocol.py ─▶ protocol.py
```

### 3-2. `protocol.py` — 바이너리 통신 프레임

STM32 `frame.c`와 **같은 규약의 Python 구현**. 먼저 만들어지고 29개 테스트로 검증되어
**"정답 기준"** 역할. 프레임 구조·CRC·상태머신은 2-6절(frame.c)과 동일하므로 생략.
대응표:

| 개념   | Python                                             | C                                             |
| ------ | -------------------------------------------------- | --------------------------------------------- |
| CRC    | `crc16_ccitt()`                                    | `fcc_crc16()`                                 |
| 인코더 | `encode_frame()`                                   | `fcc_encode_frame()`                          |
| 파서   | `FrameParser.feed()` (제너레이터, 임의 청크 yield) | `frame_parser_feed()` (바이트 1개, true 반환) |
| 측정   | `MeasPayload` (`<I f f f f>`)                      | `FccMeasPayload` (packed)                     |
| 트랙   | `TrackPayload` (`<I B f f f f>`)                   | `FccTrackPayload` (packed)                    |

```python
def crc16_ccitt(data: bytes) -> int:
    crc = 0x0000
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) if crc & 0x8000 else (crc << 1)
            crc &= 0xFFFF
    return crc

def encode_frame(msg_type, payload) -> bytes:
    length = len(payload)
    crc = crc16_ccitt(bytes([length, msg_type]) + payload)
    return bytes([STX, length, msg_type]) + payload + struct.pack(">H", crc)
```

페이로드는 `@dataclass`로 정의되고 대칭적인 `encode()`/`decode()`를 갖는다.

> **C 측 주의**: Python `struct '<'`는 패딩 없음. C 구조체엔 `__attribute__((packed))` 필수
> (특히 `TrackPayload`의 uint8 앞 uint32 → 패킹 없으면 3바이트 패딩 삽입).

### 3-3. `targets.py` — 표적과 센서 모델

#### `Target` — 등속(CV) 운동 모델

```python
@dataclass
class Target:
    target_id: int; x_m: float; y_m: float; vx_mps: float; vy_mps: float
    def update(self, dt):
        self.x_m += self.vx_mps * dt   # x = x₀ + v·dt
        self.y_m += self.vy_mps * dt
    @property
    def range_m(self):     return math.hypot(self.x_m, self.y_m)   # √(x²+y²)
    @property
    def azimuth_rad(self): return math.atan2(self.x_m, self.y_m)   # 정북 기준 시계방향
    def radial_velocity(self):                                      # 도플러
        r = self.range_m
        return 0.0 if r < 1e-6 else (self.x_m*self.vx_mps + self.y_m*self.vy_mps)/r
```

좌표계: 레이더 원점(0,0), x=동/y=북. 레이더는 극좌표(range, azimuth)로 측정.
`radial_velocity`는 속도벡터를 시선 단위벡터에 정사영 — 다가오면 음수, 멀어지면 양수.

#### `RadarSensorModel` — 레이더 불완전함 시뮬레이션

```python
@dataclass
class RadarSensorModel:
    sigma_range_m=10.0; sigma_azimuth_rad=0.01; sigma_doppler_mps=0.5
    pd=0.9; clutter_rate=0.5; max_range_m=30_000.0

    def measure(self, targets, timestamp_ms, rng):
        ms = []
        for tgt in targets:
            if tgt.range_m > self.max_range_m: continue   # 너무 멀면 안 보임
            if rng.random() > self.pd:        continue   # Pd 확률로 미탐지
            r   = tgt.range_m     + rng.gauss(0, self.sigma_range_m)      # 가우시안 노이즈
            az  = tgt.azimuth_rad + rng.gauss(0, self.sigma_azimuth_rad)
            dop = tgt.radial_velocity() + rng.gauss(0, self.sigma_doppler_mps)
            ms.append(MeasPayload(timestamp_ms, max(0.0,r), az, tgt.elevation_rad, dop))
        for _ in range(_poisson(self.clutter_rate, rng)):   # 클러터(오탐) Poisson 개수
            ms.append(MeasPayload(timestamp_ms, rng.uniform(0, self.max_range_m),
                      rng.uniform(-math.pi, math.pi), 0.0, rng.gauss(0, self.sigma_doppler_mps*3)))
        rng.shuffle(ms)        # 순서 무작위 (데이터 연관이 순서에 의존 못하게)
        return ms
```

실제 레이더의 불완전함 3종을 주입: **가우시안 노이즈**, **미탐지(Pd)**, **클러터(오탐)**.
`rng.shuffle`로 순서를 섞어 트래커가 리스트 순서를 단서로 못 쓰게 한다(실제 레이더도 순서 무의미).
`rng`를 인자로 받아 시드 고정 → 재현성 확보.

`_poisson()`은 **Knuth 알고리즘**(균등난수를 곱해 `e^(-λ)` 아래로 떨어질 때까지 카운트).

#### 시나리오 3종

```python
scenario_single_approach()  # 표적1 정면 접근 (쉬움)
scenario_two_crossing()     # 표적2 교차 (데이터연관/track-swap 검증, 대표)
scenario_four_targets()     # 표적4 동시 (부하 테스트)
```

`two_crossing`이 핵심: 두 표적 교차 시 ID 바꿔치기(track swap) 검증. NN은 흔히 실패, GNN은 버팀.

### 3-4. `transport.py` — 전송 계층 추상화

`run_sim.py`가 "실제 UART인지 Mock인지" 신경 안 쓰게 인터페이스 통일(`send`/`recv`/`close`).

- **`MockTransport`**: `queue.Queue` 기반 인메모리 루프백. `send`한 게 `recv`로 돌아옴.
  보드 없이 PC 단독 테스트(`--mock`). `inject()`로 테스트 데이터 직접 주입.
- **`SerialTransport`**: `pyserial`로 `/dev/ttyUSB0` 제어. `timeout=1.0`이라 데이터 없으면
  `recv`가 짧게 반환. `flush()`로 OS 버퍼 즉시 밀어냄. `with` 문 지원.

> 추상화 가치: 같은 `run()` 코드가 Mock·Serial 양쪽에서 그대로 돈다(의존성 역전).

### 3-5. `run_sim.py` — 메인 루프

```python
def run(transport, scenario_name, dt, duration, pd, clutter_rate, seed):
    rng = random.Random(seed)                  # 시드 고정 → 재현
    targets = SCENARIOS[scenario_name]()
    sensor  = RadarSensorModel(pd=pd, clutter_rate=clutter_rate)
    parser  = FrameParser()
    transport.send(encode_frame(MSG_CTRL, CtrlPayload(CTRL_START).encode()))  # START
    while t_elapsed < duration:
        for tgt in targets: tgt.update(dt)                       # ① 표적 전진
        meas_list = sensor.measure(targets, timestamp_ms, rng)   # ② 측정 생성
        for meas in meas_list:                                   # ③ 측정 송신
            transport.send(encode_frame(MSG_MEAS, meas.encode()))
        raw = transport.recv(n=512)                              # ④ 트랙 수신
        for msg_type, payload in parser.feed(raw):
            if msg_type == MSG_TRACK:
                _print_track(TrackPayload.decode(payload), targets, timestamp_ms)
        ...  # ⑤ dt 주기 맞추기 (time.monotonic + sleep)
    finally:
        transport.send(encode_frame(MSG_CTRL, CtrlPayload(CTRL_STOP).encode()))  # STOP
```

매 스텝: 전진 → 측정 → 송신 → 수신·파싱 → 주기유지. 시작/종료에 CTRL START/STOP.
`_print_track()`은 수신 트랙과 **가장 가까운 ground truth 표적과의 거리(gt_err)** 출력 —
시간에 걸쳐 모으면 **RMSE**가 된다(정량 검증 핵심).

CLI: `--mock`/`--serial PORT`(택1), `--scenario`, `--dt`, `--duration`, `--pd`, `--clutter`, `--seed`.

### 3-6. `test_protocol.py` — 유닛테스트 (29개)

`python -m pytest test_protocol.py -v`. 9개 카테고리:

| #   | 카테고리        | 확인 내용                                          |
| --- | --------------- | -------------------------------------------------- |
| 1   | CRC16           | 표준값 0x31C3, 빈 입력, 단일 바이트                |
| 2   | 페이로드 크기   | MEAS=20, TRACK=21, CTRL=1                          |
| 3   | 라운드트립      | encode→parse→decode 후 값 보존 (float은 근사 비교) |
| 4   | 프레임 구조     | STX 위치, LEN/TYPE 필드, 총 크기 5+LEN             |
| 5   | 가비지 재동기화 | 앞뒤 가비지·가짜 STX 있어도 프레임 찾음            |
| 6   | CRC 거부        | CRC 1비트만 깨져도 프레임 폐기                     |
| 7   | split-read      | 1바이트씩/절반씩 나눠 넣어도 조립                  |
| 8   | 다중 프레임     | 연속·혼합 타입 모두 파싱, 순서 보존                |
| 9   | reset()         | 중간상태 초기화 후 정상 파싱                       |

이 `protocol.py`는 C 구현의 **정답 기준**이다. Python 테스트를 C도 통과해야 프로토콜 호환.
특히 5·6·7번이 실전 UART의 3대 깨짐(가비지·비트오류·스트림 분할)을 막는 견고성 테스트.

---

## 4. 빌드·실행 방법 요약

### STM32 앱

```bash
source ~/zephyrproject/.venv/bin/activate         # 가상환경 활성화
cd ~/zephyrproject
west build -p always -b stm32f746g_disco /path/to/radar-track-fcc-hil/fcc_app
west flash --runner openocd                       # 반드시 openocd 러너
tio /dev/ttyACM0                                  # 시리얼 콘솔 (115200 8N1)
```

예상 부팅 출력:

```
=== FCC HIL booting ===
[uart_rx ] USART6 준비 완료, HIL 수신 시작
[display ] 활성 트랙: 0
...
```

### PC 시뮬레이터

```bash
cd pc_sim
python -m pytest test_protocol.py -v              # 테스트 29개
python run_sim.py --mock --scenario single_approach   # 보드 없이
python run_sim.py --serial /dev/ttyUSB0 --scenario single_approach  # 실제 HIL
```

### HIL 배선

```
PC ─USB─▶ ST-Link ─USART1─▶ /dev/ttyACM0   [콘솔/로그]
PC ─USB─▶ USB-UART 어댑터 ──┬─ 어댑터 RX ◀─ 보드 D1(PG14,TX)
                            └─ 어댑터 TX ─▶ 보드 D0(PG9,RX)   → /dev/ttyUSB0  [데이터]
```

TX↔RX 교차 연결 + 공통 GND 필수.

---

## 5. 검증 사다리 & 다음 단계

작업 원칙: **작은 검증 사다리**. 각 단계는 직전 단계 위에서 검증 가능, 매 단계 끝에
"보드에서 어떻게 확인하는지" 명시.

```
[칸 0] pytest 29개 통과            → 프로토콜 코덱(Python)이 명세대로
[칸 1] 부팅 로그                   → 3-스레드 + USART6 초기화 성공
[칸 2] [tracking] #N t=.. r=.. 로그 → RX 파이프라인 정상 (PC→ISR→파서→큐→tracking)
[칸 3] PC에 [TRACK] ID0 더미 에코   → TX 파이프라인 정상 (tracking→encode→UART→PC)
[칸 4~] 칼만/트랙관리 후           → 활성 트랙 N>0, gt_err 감소, RMSE 정량화
```

칸 2·3이 현재 도달점 — RX→처리→TX 왕복 전체가 더미 에코로 검증됨.

### 진행 상태 (정직하게)

```
[x] PC 시뮬레이터 (프레임 코덱, 센서 모델, 전송 추상화, 유닛테스트 29개)
[x] STM32 3-스레드 골격 + UART 프레이밍 (CRC16, 인코더, FrameParser, ISR+링버퍼)
[ ] 칼만 필터 포팅            ← 다음. tracking_thread 더미 에코 → 진짜 추정
[ ] 트랙 관리 (M-of-N, 데이터 연관)  ← g_track_table 실제 갱신 → display에 트랙 수
[ ] LVGL PPI 디스플레이        ← display_thread → LCD 렌더링
[ ] HIL 통합 + RMSE 정량 검증   ← _print_track gt_err 모아 RMSE 산출
```

**M-of-N**: N번 스캔 중 M번 이상 탐지돼야 "진짜 표적" 확정. 클러터는 보통 연속으로 같은
위치에 안 잡히므로 이걸로 걸러냄.

### 작업 원칙 (CLAUDE.md)

- 골격 먼저, 살은 단계별로. 한 번에 다 만들지 않는다.
- 인라인 주석/문서는 한국어.
- 진행 상태를 정직하게 유지 (안 되는 건 주석에 명시).
- 인증 아티팩트(DO-178C 등)를 코드에서 자동 생성하지 말 것 — 별도 트랙, 자동생성은 안티패턴.
