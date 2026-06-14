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
┌─────────────────────────────────────┐
│              PC (Python)             │
│  "표적이 여기 있어" → 측정값 생성  │
│  → UART로 STM32 전송               │
│                                     │
│  STM32에서 트랙 결과 수신          │
│  → 실제 위치와 오차(RMSE) 계산    │
└──────────────┬──────────────────────┘
               │ UART (USB-to-Serial)
               │ 115200 bps
┌──────────────▼──────────────────────┐
│         STM32F746G-DISCO            │
│  "측정값 받아서 추적 계산해줄게"   │
│  → 칼만 필터 → 트랙 결과 반환     │
└─────────────────────────────────────┘
```

**현재 구현 단계**: 아직 추적 알고리즘은 없다. 뼈대(골격)만 잡은 상태.
- STM32: 3개 스레드가 살아있고, 통신 구조물이 선언돼 있음
- PC: 시뮬레이터 + 통신 프레임 코덱이 완성됨

---

## 1. 전체 파일 구조

```
radar-track-fcc-hil/
│
├── fcc_app/                  ← STM32에 올라가는 C 프로그램
│   ├── CMakeLists.txt        ← 빌드 설정
│   ├── prj.conf              ← RTOS 기능 켜기/끄기 설정
│   └── src/
│       └── main.c            ← 메인 코드 (스레드 3개)
│
└── pc_sim/                   ← PC에서 돌아가는 Python 시뮬레이터
    ├── protocol.py           ← 바이너리 통신 프레임 코덱
    ├── targets.py            ← 표적 운동 모델 + 레이더 센서 모델
    ├── transport.py          ← UART 추상화 (Mock / 실제 Serial)
    ├── run_sim.py            ← 시뮬레이터 메인 루프
    └── test_protocol.py      ← 유닛테스트 (pytest)
```

---

## 2. STM32 측 — `fcc_app/`

### 2-1. 배경: Zephyr RTOS란

STM32는 일반 PC가 아니다. RAM이 256KB, CPU가 216MHz. 여기서 OS 없이
코딩하면 `while(1)` 루프 하나만 돌 수 있다.

**Zephyr**는 이런 작은 칩용 RTOS(Real-Time Operating System)다.
Linux처럼 스레드, 뮤텍스, 메시지큐를 제공하는데, 커널 크기가 수십 KB에 불과하다.

Zephyr 앱을 만들려면 파일 3개가 필요하다:
- `CMakeLists.txt`: "이 소스파일로 앱 빌드해줘" → cmake/ninja가 읽음
- `prj.conf`: "뮤텍스 기능 켜줘, 스레드 이름 출력 켜줘" → Kconfig 시스템이 읽음
- `src/main.c`: 실제 코드

### 2-2. `CMakeLists.txt`

```cmake
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(fcc_app)
target_sources(app PRIVATE src/main.c)
```

`find_package(Zephyr)` 한 줄이 핵심. Zephyr SDK를 찾아서 컴파일러,
링커 스크립트, HAL 라이브러리를 모두 설정해준다.
`target_sources`는 "src/main.c를 빌드 대상에 추가"라는 cmake 문법.

### 2-3. `prj.conf`

```
CONFIG_PRINTK=y        # printk() 함수 활성화 (콘솔 출력)
CONFIG_LOG=y           # 로그 서브시스템
CONFIG_THREAD_NAME=y   # 스레드에 이름 붙이기 (디버깅용)
CONFIG_THREAD_STACK_INFO=y  # 스택 사용량 확인 가능
```

Kconfig는 Linux 커널에서 유래한 설정 시스템.
`CONFIG_XXX=y`로 기능을 켜면 컴파일 시 해당 코드가 포함된다.
`=n`이거나 없으면 코드 자체가 바이너리에 안 들어간다. 크기를 줄이기 위해.

### 2-4. `src/main.c` — 핵심 파일

#### 전체 구조 한눈에 보기

```
main.c
│
├── struct meas_msg          ← 스레드 간 메시지 구조체 (placeholder)
├── K_MSGQ_DEFINE(meas_msgq) ← 메시지큐 선언
│
├── struct track             ← 개별 트랙 구조체 (placeholder)
├── struct track_table       ← 트랙 테이블 (배열)
├── g_track_table            ← 전역 공유 데이터
├── K_MUTEX_DEFINE(...)      ← 뮤텍스 선언
│
├── 스택 공간 3개 선언
├── k_thread 구조체 3개 선언
│
├── uart_rx_thread()         ← 스레드 함수 1
├── tracking_thread()        ← 스레드 함수 2
├── display_thread()         ← 스레드 함수 3
│
└── main()                   ← 3개 스레드 생성 후 반환
```

#### 스레드가 왜 3개인가

레이더 시스템에서 동시에 해야 할 일이 3가지다:

| 스레드 | 하는 일 | 지금 상태 |
|--------|---------|-----------|
| `uart_rx` | PC에서 측정값 받기 | 1초마다 로그 출력 |
| `tracking` | 칼만 필터로 위치 계산 | 1초마다 로그 출력 |
| `display` | LCD에 트랙 화면 그리기 | 1초마다 로그 출력 |

세 가지를 `while(1)` 하나로 돌리면 UART 수신 중에 화면이 멈추고,
화면 그리는 중에 UART 데이터를 놓치는 문제가 생긴다.
스레드로 분리하면 Zephyr 스케줄러가 번갈아 실행해준다.

#### 스레드 생성 코드 분석

```c
// 1. 스택 공간 할당 (각 1024바이트)
K_THREAD_STACK_DEFINE(uart_rx_stack, STACK_SIZE);

// 2. 스레드 제어 블록 선언
static struct k_thread uart_rx_td;

// 3. 스레드 생성
k_thread_create(
    &uart_rx_td,               // 스레드 제어 블록 포인터
    uart_rx_stack,             // 스택 시작 주소
    K_THREAD_STACK_SIZEOF(...),// 스택 크기
    uart_rx_thread,            // 실행할 함수
    NULL, NULL, NULL,          // 함수 인자 3개 (지금은 사용 안 함)
    5,                         // 우선순위 (낮은 숫자 = 높은 우선순위)
    0,                         // 플래그
    K_NO_WAIT                  // 즉시 시작
);
k_thread_name_set(&uart_rx_td, "uart_rx");
```

OS 수업에서 배운 `pthread_create`와 개념이 같다.
다른 점: 스택을 직접 줘야 한다 (heap이 없거나 극히 제한적이라서).

우선순위: `uart_rx=5`, `tracking=6`, `display=7`.
숫자가 작을수록 우선순위 높음.
UART 수신이 가장 중요하니까 우선순위를 가장 높게.

#### 스레드 함수

```c
static void uart_rx_thread(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);  // 경고 억제
    uint32_t cnt = 0;
    while (1) {
        printk("[uart_rx ] tick %u\n", cnt++);
        k_sleep(K_MSEC(1000));  // 1초 sleep
    }
}
```

`k_sleep()`은 Linux의 `sleep()`과 같지만 Zephyr 커널에게 "나 1초 안 써도 돼,
다른 스레드 써" 라고 양보하는 것. CPU 낭비 없이 3개가 번갈아 실행된다.

현재는 지금 빌드·실행되는지 확인하는 뼈대이므로 로그만 출력.

#### 메시지큐 (K_MSGQ)

```c
struct meas_msg {
    uint32_t timestamp_ms;
    // 나중에: range, azimuth, elevation, doppler 추가
};

K_MSGQ_DEFINE(meas_msgq, sizeof(struct meas_msg), 8, 4);
//                        ↑ 메시지 1개 크기    ↑ 슬롯 수  ↑ 정렬 바이트
```

OS에서 배운 **Bounded Buffer** 문제 그대로다.
- `uart_rx` 스레드: `k_msgq_put(&meas_msgq, &msg, K_NO_WAIT)` — 측정값 넣기
- `tracking` 스레드: `k_msgq_get(&meas_msgq, &msg, K_FOREVER)` — 측정값 꺼내기

슬롯이 8개이므로, `tracking`이 처리하기 전에 최대 8개까지 버퍼링된다.
슬롯이 꽉 차면 `k_msgq_put`이 실패(또는 블록).

#### 뮤텍스 + 공유 트랙 테이블

```c
struct track_table {
    struct track tracks[MAX_TRACKS];  // MAX_TRACKS = 8
};

static struct track_table g_track_table;  // 전역 공유 변수
static K_MUTEX_DEFINE(g_track_mutex);      // 이를 보호하는 뮤텍스
```

`tracking` 스레드가 쓰고, `display` 스레드가 읽는 공유 데이터다.
동시 접근하면 데이터가 깨질 수 있으므로 뮤텍스로 보호.

나중 구현 예시:
```c
// tracking 스레드에서
k_mutex_lock(&g_track_mutex, K_FOREVER);
g_track_table.tracks[i] = updated_track;
k_mutex_unlock(&g_track_mutex);

// display 스레드에서
k_mutex_lock(&g_track_mutex, K_FOREVER);
draw(g_track_table.tracks);  // read-only
k_mutex_unlock(&g_track_mutex);
```

---

## 3. PC 측 — `pc_sim/`

### 3-1. 전체 의존 관계

```
run_sim.py
  ├── protocol.py    (프레임 인코딩/디코딩)
  ├── targets.py     (표적 + 센서 모델)
  └── transport.py   (보내기/받기)

test_protocol.py
  └── protocol.py
```

### 3-2. `protocol.py` — 바이너리 통신 프레임

#### 왜 바이너리인가

UART로 문자열 `"range=1234.5,az=0.31\n"` 같이 보낼 수도 있다.
하지만 실제 시스템에서는:
- **크기**: float 하나를 ASCII로 보내면 8~12바이트, 바이너리는 4바이트
- **파싱 속도**: STM32에서 `atof()` 파싱은 느리고 오류 가능성 높음
- **오류 감지**: 바이너리 + CRC로 데이터 깨짐을 검출할 수 있음

그래서 **바이너리 프레임**을 정의했다.

#### 프레임 구조

```
바이트 위치:   0      1      2      3 ~ 3+N    3+N+0  3+N+1
              ┌──────┬──────┬──────┬──────────┬───────┬───────┐
              │ STX  │ LEN  │ TYPE │ PAYLOAD  │ CRC_H │ CRC_L │
              │ 0x7E │  N   │ 0x01 │ N 바이트 │       │       │
              └──────┴──────┴──────┴──────────┴───────┴───────┘
```

- **STX (0x7E)**: "프레임 시작"을 알리는 마커 바이트. 수신측이 여기서부터 읽기 시작.
- **LEN**: 뒤에 오는 PAYLOAD의 바이트 수 (0~64). 이 숫자만큼 더 읽으면 됨.
- **TYPE**: 메시지 종류. `0x01`=측정값, `0x02`=트랙, `0x03`=제어명령
- **PAYLOAD**: 실제 데이터 (크기는 TYPE마다 고정)
- **CRC16**: 체크섬 2바이트 (big-endian). LEN+TYPE+PAYLOAD에 대해 계산.

**총 프레임 크기**: `5 + LEN` 바이트

예: 측정값(MEAS) 프레임 = `5 + 20 = 25바이트`

#### 메시지 타입별 페이로드

```
MSG_MEAS (0x01) — PC → STM32, 20바이트
  [0~3]  uint32  timestamp_ms    시각 (밀리초)
  [4~7]  float32 range_m         거리 (미터)
  [8~11] float32 azimuth_rad     방위각 (라디안)
  [12~15] float32 elevation_rad  앙각 (라디안)
  [16~19] float32 doppler_mps   도플러 속도 (m/s)

MSG_TRACK (0x02) — STM32 → PC, 21바이트
  [0~3]  uint32  timestamp_ms
  [4]    uint8   track_id        추적 번호 (0~255)
  [5~8]  float32 x_m            동쪽 좌표
  [9~12] float32 y_m            북쪽 좌표
  [13~16] float32 vx_mps        동쪽 속도
  [17~20] float32 vy_mps        북쪽 속도

MSG_CTRL (0x03) — 양방향, 1바이트
  [0]    uint8   cmd             0x01=시작, 0x02=정지, 0x03=리셋
```

모두 **little-endian**이다 (ARM Cortex-M이 little-endian이라서).
Python에서는 `struct.pack("<I f f f f", ...)` 로 인코딩.

> **C 측 주의**: Python의 `struct`는 패딩을 넣지 않는다.
> C 구조체는 `__attribute__((packed))`를 붙여야 같은 레이아웃이 된다.
> 예: `uint32_t(4바이트)` 뒤에 `uint8_t(1바이트)`가 오면, C는 기본적으로
> 3바이트 패딩을 삽입해 다음 `float`를 4바이트 경계에 맞춘다.

#### `crc16_ccitt(data: bytes) -> int`

```python
def crc16_ccitt(data: bytes) -> int:
    crc = 0x0000
    for byte in data:
        crc ^= byte << 8        # 현재 바이트를 CRC 상위 8비트에 XOR
        for _ in range(8):      # 8번 비트 시프트
            crc = ((crc << 1) ^ 0x1021) if crc & 0x8000 else (crc << 1)
            crc &= 0xFFFF       # 16비트 유지
    return crc
```

CRC(Cyclic Redundancy Check)는 데이터 오류를 감지하는 체크섬.
`0x1021`은 CRC16-CCITT의 생성 다항식. 알려진 검증값: `crc16_ccitt(b"123456789") == 0x31C3`.

이 함수는 나중에 C로 그대로 포팅된다:
```c
uint16_t crc16_ccitt(const uint8_t *data, uint16_t len) {
    uint16_t crc = 0x0000;
    for (uint16_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int j = 0; j < 8; j++)
            crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : (crc << 1);
    }
    return crc;
}
```

#### `encode_frame(msg_type, payload) -> bytes`

```python
def encode_frame(msg_type: int, payload: bytes) -> bytes:
    length = len(payload)
    crc = crc16_ccitt(bytes([length, msg_type]) + payload)
    return bytes([STX, length, msg_type]) + payload + struct.pack(">H", crc)
#                                                                  ↑ big-endian 2바이트
```

`struct.pack(">H", crc)`: `>`는 big-endian, `H`는 unsigned short(2바이트).
CRC는 big-endian으로 전송 (네트워크 바이트 오더 관례).

#### `FrameParser` — 상태머신

UART는 바이트 스트림이다. 프레임 경계가 없다.
"어디서부터 어디까지가 한 프레임인지"를 알아내는 게 파서의 역할.

**상태머신 6개 상태:**

```
WAIT_STX → WAIT_LEN → WAIT_TYPE → WAIT_PAYLOAD → WAIT_CRC_H → WAIT_CRC_L
   ↑____________________________________________CRC 불일치 시 여기로____________↑
```

```python
# 각 상태에서 하는 일:

WAIT_STX:      0x7E 바이트를 기다린다. 다른 바이트는 버린다. ← "가비지 처리"
WAIT_LEN:      다음 바이트 = LEN. 단, LEN > 64이면 가비지 취급 → WAIT_STX로
WAIT_TYPE:     다음 바이트 = TYPE
WAIT_PAYLOAD:  LEN개 바이트를 버퍼에 모은다
WAIT_CRC_H:    CRC 상위 바이트 저장
WAIT_CRC_L:    CRC 하위 바이트 받아서 검증.
               OK → (TYPE, payload) yield
               FAIL → 버리고 WAIT_STX로 (재동기화)
```

**`MAX_PAYLOAD = 64` 체크의 이유:**

가비지 데이터 안에 `0x7E`가 있으면 파서가 그걸 STX로 착각한다.
만약 그 다음 바이트가 `0xAB(=171)`이면, 파서는 "171바이트짜리 페이로드를 기다려야지"
라고 생각하고 실제 프레임 6바이트를 페이로드로 삼켜버린다.

`LEN > 64`면 "이건 정상적인 프레임이 아니다, 버리고 다시 STX 찾자"로 처리.
우리 프로토콜 최대 페이로드는 21바이트이므로 64는 충분한 여유.

**`feed()` 사용법:**

```python
parser = FrameParser()
for msg_type, payload in parser.feed(raw_bytes):
    # 완성된 프레임 처리
    if msg_type == MSG_MEAS:
        meas = MeasPayload.decode(payload)
```

임의 크기 청크로 호출 가능. 한 바이트씩 불러도 되고, 한 번에 100바이트를 줘도 된다.
파서 내부에 상태가 유지되므로 스트림을 나눠서 넣어도 자동으로 조립.

#### 페이로드 데이터클래스

```python
@dataclass
class MeasPayload:
    timestamp_ms: int
    range_m: float
    # ...

    def encode(self) -> bytes:       # 구조체 → 바이트
    def decode(cls, data) -> "MeasPayload":  # 바이트 → 구조체
```

Python의 `@dataclass`는 `__init__`, `__repr__` 등을 자동 생성.
`encode()`/`decode()`가 있어서 직렬화/역직렬화가 대칭적.

---

### 3-3. `targets.py` — 표적과 센서 모델

#### `Target` 클래스

```python
@dataclass
class Target:
    target_id: int
    x_m: float    # 동쪽 좌표 (m)
    y_m: float    # 북쪽 좌표 (m)
    vx_mps: float # 동쪽 속도 (m/s)
    vy_mps: float # 북쪽 속도 (m/s)

    def update(self, dt: float):
        self.x_m += self.vx_mps * dt  # 등속 운동: x = x₀ + v·dt
        self.y_m += self.vy_mps * dt
```

**등속 운동(Constant Velocity, CV) 모델**: 가속도가 없다고 가정.
현실 비행기는 기동하지만, 첫 구현에서는 단순화.

**좌표계**: 레이더가 원점(0,0)에 있고, x=동, y=북.

**`range_m` 프로퍼티**: `sqrt(x² + y²)` — 레이더까지의 거리

**`azimuth_rad` 프로퍼티**: `atan2(x, y)` — 정북 기준 시계방향 각도
- y축 정북이면 `atan2(0, y) = 0`
- 정동 방향이면 `atan2(x, 0) = π/2`

**`radial_velocity()`**: 도플러 속도 (시선 방향 속도).
```
v_r = (x·vx + y·vy) / r
```
내적을 거리로 나눈 것. 물리적으로: 레이더를 향해 다가오면 음수, 멀어지면 양수.

#### `RadarSensorModel` 클래스

실제 레이더의 불완전함을 시뮬레이션.

```python
@dataclass
class RadarSensorModel:
    sigma_range_m: float = 10.0      # range 측정 오차 표준편차
    sigma_azimuth_rad: float = 0.01  # 방위각 측정 오차 표준편차
    sigma_doppler_mps: float = 0.5   # 도플러 오차
    pd: float = 0.9                  # 탐지 확률 (Probability of Detection)
    clutter_rate: float = 0.5        # 스캔당 평균 오탐 개수
```

**`measure(targets, timestamp_ms, rng) -> list`:**

```python
for tgt in targets:
    # 1. 거리 밖 표적 무시
    if tgt.range_m > self.max_range_m: continue

    # 2. 미탐지 (Pd=0.9 → 10% 확률로 못 봄)
    if rng.random() > self.pd: continue

    # 3. 가우시안 측정 노이즈 추가
    r   = tgt.range_m     + rng.gauss(0, self.sigma_range_m)
    az  = tgt.azimuth_rad + rng.gauss(0, self.sigma_azimuth_rad)
    dop = tgt.radial_velocity() + rng.gauss(0, self.sigma_doppler_mps)
```

`rng.gauss(0, σ)`: 평균 0, 표준편차 σ인 정규분포 샘플.
레이더 측정 오차는 가우시안이라고 모델링하는 것이 표준 관례.

**클러터(오탐) 주입:**
```python
n_clutter = _poisson(self.clutter_rate, rng)  # 예: 평균 0.5개
for _ in range(n_clutter):
    r  = rng.uniform(0, max_range)  # 랜덤 위치
    az = rng.uniform(-π, π)
```

새, 지형 반사 등으로 생기는 가짜 탐지. 개수가 확률적이라 Poisson 분포 사용.

**`_poisson(lam, rng)` 내부:**

Knuth 알고리즘. 수학적으로 Poisson 분포에서 샘플링하는 방법:
```
L = e^(-λ), k=0, p=1
while p > L: k++, p *= uniform(0,1)
return k-1
```

결과를 **섞어서 반환** (`rng.shuffle`): 추적 알고리즘이 순서를 단서로
쓰면 안 되기 때문. 실제 레이더도 측정값 순서가 랜덤.

#### 시나리오 3가지

```python
scenario_single_approach()   # 표적 1개, 정면 접근
scenario_two_crossing()      # 표적 2개가 교차 (어려운 케이스)
scenario_four_targets()      # 표적 4개 동시 (부하 테스트)
```

`two_crossing`이 핵심: 두 표적이 교차할 때 추적기가 ID를 바꿔치기(track swap)하는지
검증하는 대표 시나리오.

---

### 3-4. `transport.py` — 전송 계층 추상화

보내기/받기 인터페이스를 추상화해서,
`run_sim.py`가 실제 UART인지 테스트용 Mock인지 신경 쓰지 않아도 되게 함.

#### `MockTransport`

```python
class MockTransport:
    def __init__(self):
        self._q = queue.Queue()  # Python 표준 쓰레드-세이프 큐

    def send(self, data):
        self._q.put(data)        # 큐에 넣기

    def recv(self, n=256, timeout=1.0):
        chunk = self._q.get(timeout=timeout)  # 큐에서 꺼내기
        return chunk[:n]

    def inject(self, data):      # 테스트용: 외부에서 수신 버퍼에 직접 주입
        self._q.put(data)
```

`queue.Queue`는 Python 내장 쓰레드-세이프 FIFO.
`send()`로 넣은 데이터가 `recv()`로 나온다 → 루프백.

STM32가 없어도 `run_sim.py --mock`으로 PC 단독 테스트 가능.
(물론 트랙 결과는 안 돌아오지만, 측정값 생성·인코딩·전송 흐름 확인 가능)

#### `SerialTransport`

```python
class SerialTransport:
    def __init__(self, port, baudrate=115200):
        import serial
        self._ser = serial.Serial(port, baudrate=baudrate, timeout=1.0)

    def send(self, data):
        self._ser.write(data)
        self._ser.flush()        # OS 버퍼 비우기

    def recv(self, n=256):
        return self._ser.read(n) # 최대 n바이트, timeout 후 짧을 수 있음
```

`pyserial` 라이브러리로 USB-to-Serial(ttyACM0) 포트 제어.
`timeout=1.0`: 1초 안에 데이터 없으면 `recv()`가 짧은 bytes 반환.

---

### 3-5. `run_sim.py` — 메인 루프

#### CLI 파라미터

```bash
python run_sim.py --mock --scenario two_crossing --dt 0.05 --duration 60
python run_sim.py --serial /dev/ttyACM0 --pd 0.8 --clutter 1.0
```

| 파라미터 | 기본값 | 설명 |
|---------|--------|------|
| `--mock` | - | 보드 없이 테스트 |
| `--serial PORT` | - | 실제 UART |
| `--scenario` | `two_crossing` | 시나리오 선택 |
| `--dt` | `0.05` | 스텝 간격 (초) |
| `--duration` | `60.0` | 시뮬레이션 시간 (초) |
| `--pd` | `0.9` | 탐지확률 |
| `--clutter` | `0.5` | 평균 오탐 수 |
| `--seed` | `42` | 난수 시드 (재현성) |

#### `run()` 메인 루프

```python
while t_elapsed < duration:
    # 1. 표적 전진 (물리 시뮬레이션)
    for tgt in targets:
        tgt.update(dt)

    # 2. 레이더 측정 (노이즈·미탐지·오탐 포함)
    meas_list = sensor.measure(targets, timestamp_ms, rng)

    # 3. 측정값 인코딩 → UART 송신
    for meas in meas_list:
        transport.send(encode_frame(MSG_MEAS, meas.encode()))

    # 4. STM32 트랙 수신 → 파싱
    raw = transport.recv(n=512)
    for msg_type, payload in parser.feed(raw):
        if msg_type == MSG_TRACK:
            trk = TrackPayload.decode(payload)
            _print_track(trk, targets, timestamp_ms)

    # 5. dt 주기 맞추기
    time.sleep(max(0, dt - elapsed))
```

`_print_track()`: 수신된 트랙 위치와 가장 가까운 ground truth 표적과의
거리(오차)를 출력. 이것이 나중에 RMSE 계산으로 발전.

---

### 3-6. `test_protocol.py` — 유닛테스트 (29개)

pytest로 돌린다: `python -m pytest test_protocol.py -v`

테스트 카테고리 9가지:

| 카테고리 | 테스트 내용 |
|---------|------------|
| 1. CRC16 | 알려진 검증값(0x31C3), 빈 입력, 단일 바이트 |
| 2. 페이로드 크기 | MEAS=20, TRACK=21, CTRL=1 바이트인지 |
| 3. 라운드트립 | encode → parse → decode 후 값이 같은지 |
| 4. 프레임 구조 | STX 위치, LEN 필드, TYPE 필드, 총 크기 |
| 5. 가비지 재동기화 | 앞뒤 가비지 있어도 프레임 찾는지, 가비지 속 STX가 있어도 |
| 6. CRC 거부 | CRC 1비트라도 깨지면 프레임 버리는지 |
| 7. split-read | 바이트 1개씩 / 절반씩 나눠 넣어도 조립되는지 |
| 8. 다중 프레임 | 여러 프레임 연속으로 모두 파싱되는지 |
| 9. reset() | 중간 상태 초기화 후 새 프레임 정상 파싱 |

테스트가 중요한 이유: 이 `protocol.py`는 나중에 C로 다시 구현될 때
"정답"이 된다. Python 테스트를 C 구현도 통과해야 프로토콜이 호환된다.

---

## 4. 빌드·실행 방법 요약

### STM32 앱

```bash
# 가상환경 활성화
source ~/zephyrproject/.venv/bin/activate

# 빌드
cd ~/zephyrproject
west build -p always -b stm32f746g_disco \
  /home/sungjae/바탕화면/radar-track-fcc-hil/fcc_app

# 플래시 (보드 USB 연결 상태에서)
west flash --runner openocd

# 시리얼 모니터
tio /dev/ttyACM0 -b 115200
```

예상 출력:
```
=== FCC HIL skeleton booting ===
[uart_rx ] tick 0
[tracking] tick 0
[display ] tick 0
[uart_rx ] tick 1
...
```

### PC 시뮬레이터

```bash
cd /home/sungjae/바탕화면/radar-track-fcc-hil/pc_sim

# 테스트 실행
python -m pytest test_protocol.py -v

# 보드 없이 시뮬레이터 테스트 (Mock)
python run_sim.py --mock --scenario two_crossing

# 보드 연결 시 실제 HIL 실행
python run_sim.py --serial /dev/ttyACM0
```

---

## 5. 다음 단계 (아직 안 만든 것)

현재 상태: 뼈대만 있고 스레드들이 tick만 출력함.

```
[완료] STM32 3-스레드 골격
[완료] PC 시뮬레이터 (프레임 코덱, 센서 모델, 전송 추상화)
[다음] STM32에 UART 프레임 파서 구현 (C로 FrameParser 포팅)
[다음] STM32에 칼만 필터 포팅
[다음] 트랙 관리 (M-of-N: N번 중 M번 탐지돼야 트랙 확정)
[다음] 데이터 연관 (어떤 측정값이 어떤 트랙의 것인지 판단)
[다음] LVGL 디스플레이 (LCD에 트랙 시각화)
[다음] HIL 통합 테스트 + RMSE 정량 검증
```

M-of-N 예시: 10번 스캔 중 7번 이상 탐지돼야 "진짜 표적"으로 판정.
클러터는 보통 연속으로 같은 위치에서 잡히지 않으므로 이걸로 걸러냄.
