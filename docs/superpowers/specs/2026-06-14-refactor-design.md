# Refactor Design — radar-track-fcc-hil

**날짜**: 2026-06-14  
**범위**: STM32 C 코드 모듈 분리 + 품질 이슈 수정 + Python 정리 + README 갱신

---

## 1. 아키텍처 (변경 후)

```
fcc_app/src/
    main.c          — 장치 초기화 + 3개 스레드 생성만
    uart_rx.h/c     — UART ISR, 링버퍼, FrameParser, meas_msgq 공개
    tracking.h/c    — tracking 스레드, 트랙 테이블, mutex, fcc_send_track
    display.h/c     — display 스레드
    frame.h/c       — (기존 그대로) 프레임 코덱

pc_sim/
    protocol.py     — (기존 그대로)
    targets.py      — 지연 임포트 제거 → 최상단 import로 교체
    transport.py    — (변경 없음)
    run_sim.py      — import math 최상단으로 이동

README.md           — 전면 갱신
```

---

## 2. STM32 변경 상세

### 2-1. main.c 분리

| 기존 (main.c) | 이동 대상 |
|---|---|
| `g_hil_uart`, `g_uart_rx_ring`, `g_uart_rx_sem`, `hil_uart_isr()`, `uart_rx_thread()` | `uart_rx.h/c` |
| `meas_msg`, `meas_msgq` | `uart_rx.h` (공개 심볼) |
| `g_track_table`, `g_track_mutex`, `track`, `track_table`, `fcc_send_track()`, `tracking_thread()` | `tracking.h/c` |
| `display_thread()` | `display.h/c` |
| 스택/스레드 변수, `main()` | `main.c` 잔류 |

### 2-2. meas_msg 중복 제거

현재 `meas_msg`는 `FccMeasPayload`와 필드 구성이 동일 → `meas_msgq` 타입을 `FccMeasPayload`로 변경.

```c
// 변경 전
K_MSGQ_DEFINE(meas_msgq, sizeof(struct meas_msg), 8, 4);
// 변경 후  (uart_rx.h)
K_MSGQ_DEFINE(meas_msgq, sizeof(FccMeasPayload), 8, 4);
```

`uart_rx_thread` 내 `struct meas_msg msg = { .timestamp_ms = mp->timestamp_ms, ... }` 복사 제거 →  
`FccMeasPayload` 값을 바로 `k_msgq_put`.

`tracking_thread`에서 `msg.range_m` → `mp.range_m` 으로 필드명 동일, 타입만 변경.

### 2-3. frame_parser_reset() 버그 수정

현재 `reset()`은 `state`와 `buf_idx`만 초기화하고 `len`, `msg_type`, `crc_h`를 남긴다.  
이론상 무해하지만(다음 STX부터 덮어쓰므로), 방어적으로 전체 초기화:

```c
void frame_parser_reset(FrameParser *p)
{
    p->state    = FP_WAIT_STX;
    p->buf_idx  = 0;
    p->len      = 0;
    p->msg_type = 0;
    p->crc_h    = 0;
}
```

---

## 3. Python 변경 상세

### 3-1. targets.py 지연 임포트 제거

`RadarSensorModel.measure()` 안의 `from protocol import MeasPayload`를  
파일 최상단 `from protocol import MeasPayload`로 이동.  
실제 순환 임포트가 없으므로 안전.

### 3-2. run_sim.py import math 이동

`_print_track()` 내부의 `import math`를 파일 최상단으로 이동.

---

## 4. README.md 갱신 내용

- 프로젝트 개요 (폐루프 HIL 구조 그림)
- 개발 환경 설정 (venv, SDK 경로)
- 빌드 / 플래시 / 시리얼 연결 명령
- PC 시뮬레이터 사용법 (`--mock` / `--serial`)
- 진행 상태 체크리스트
- 파일 구조 설명

---

## 5. 변경하지 않는 것

- `frame.h/c` 공개 API (CRC, encode, FrameParser) — 기능 동일
- Python `protocol.py` — 변경 없음
- Python `transport.py` — 변경 없음
- Python `test_protocol.py` — 변경 없음
- CMakeLists.txt의 빌드 설정 (단, 새 .c 파일 추가 필요)
- 프레임 포맷 / 메시지 타입 / 페이로드 레이아웃

---

## 6. 검증 계획

1. `west build -p always -b stm32f746g_disco fcc_app` 빌드 성공 확인
2. `cd pc_sim && python -m pytest test_protocol.py -v` 전체 통과 확인
3. `python run_sim.py --mock --scenario single_approach` 에러 없이 실행 확인
4. (보드 있을 때) 플래시 후 `[tracking] #N ...` 로그 수신 확인
