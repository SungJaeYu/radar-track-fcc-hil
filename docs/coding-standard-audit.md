# 코딩 스탠다드 적용 점검

2026-11-08 기준 `fcc_app/src`의 C·헤더 27개를 [코딩 스탠다드](coding-standard.md)와 비교한 수정 목록입니다.
펌웨어는 수정하지 않았습니다. 테스트 소스·Python/GUI 전체의 스타일 검토는 이 목록의 범위가 아닙니다.
2026-11-10 폴더 분리 후에도 아래 위치는 파일명과 함수/심볼 기준입니다.
현재 소속은 transport(UART·RX processor), protocol(parser·frame·message),
application(handler), tracking(track·filter·gating·angle), common(CRC·ring)입니다. 단순 표기 정리와 계약·동작 변경을 분리해서 적용합니다.

## 1. 오류 반환·직접 의존성 — 우선 적용

- [ ] `uart_rx.c: uart_rx_init()` — 장치 미준비 시 `-1`을 `-ENODEV`로 변경.
  callback 등록 결과는 지역변수로 받아 원래 음수 errno를 로그와 반환값에 보존.
- [ ] `uart_tx.c: uart_tx_init()` — 장치 미준비 시 `-ENODEV` 반환.
- [ ] `main.c: main()` — TX/RX init 오류를 각각 받아 코드와 함께 로그를 남기고 반환.
  현재 `!= 0` 검사 후 `-1`로 반환해 원인을 잃음. main 반환값이 외부에 전달되는 방식과 별개로 원인 로그를 보존.
- [ ] `frame_handler.c`, `rx_frame_processor.c` — 직접 사용하는 `EINVAL`/`ENOMSG`의 `<errno.h>` include 추가.
- [ ] `uart_tx.c` — `printk()`의 `<zephyr/sys/printk.h>` 직접 include 추가.
- [ ] 위 init 반환값 변경 시 RX/TX host 테스트의 `-1` 기대값과 실패 경로를 함께 갱신.

## 2. 이름 — 헤더·구현·호출부 함께 변경

### 출력 포인터와 선언 일치

| 파일·함수 | 현재 | 목표 |
|---|---|---|
| `protocol_frame.c/.h: protocol_frame_encode` | `p_buffer` | `out_buffer` |
| `radar_message.c/.h: decode_radar_measurement` | `p_measurement` | `out_measurement` |
| `ring_buffer.c/.h: ring_pop` | `p_c` | `out_byte` |
| `rx_frame_processor.c/.h: rx_frame_processor_get_stats` | `p_stats` | `out_stats` |
| `frame_handler.c/.h: handle_frame` | 헤더 `p_frame`, 구현 `p_r_frame` | `p_frame`으로 일치 |
| `frame_handler.c: handle_radar_measurement` | 전방 선언 `p_frame`, 정의 `p_r_frame` | `p_frame`으로 일치 |
| `uart_tx.c/.h: uart_tx_submit` | 헤더 `p_frame`, 구현 `p_s_frame` | `p_frame`으로 일치 |

- [ ] 위 표 적용. parser·track·filter·ring 등 상태 객체의 `p_`는 유지.
  `validate_radar_measurement()`의 읽기 전용 `p_measurement`도 유지.

### enum과 상수

enum 타입 `FrameType`, `ParserState`, `ParserResult`, `TrackState`, `RxOverflowPolicy`는 이미 PascalCase입니다.
2026-11-10 열거자 접두사 적용 완료. 아래 화살표 왼쪽 이름은 변경 이력이며 숫자 값·로그 문자열은 유지했습니다.

- [x] `protocol_frame.h`: `TYPE_*` → `FRAME_TYPE_*` (예: `FRAME_TYPE_RADAR_MEAS`). 숫자 0x01~0x05 보존.
- [x] `frame_parser.h`: `WAIT_*`, `READ_PAYLOAD` → `PARSER_STATE_WAIT_*`, `PARSER_STATE_READ_PAYLOAD`.
- [x] `frame_parser.h`: `PARSER_*` 결과 → `PARSER_RESULT_*`.
- [x] `track.h`: `TRACK_*` 상태 → `TRACK_STATE_*`. `TRACK_CONFIRM_HITS` 같은 설정 상수는 별개.
- [x] `uart_rx.h`: `RX_OVERFLOW_DISCARD_ALL/DRAIN_BUFFER` → `RX_OVERFLOW_POLICY_DISCARD_ALL/DRAIN_BUFFER`.
  선택 매크로 `RX_OVERFLOW_POLICY`와 enum 값의 역할을 구분.
- [ ] `STX`, `MAX_PAYLOAD`, `BUFFER_SIZE`, `INIT_CRC`에 모듈 접두사 적용 권장:
  `PROTOCOL_STX`, `PROTOCOL_MAX_PAYLOAD`, `RING_BUFFER_SIZE`, `CRC16_INITIAL_VALUE`.
- [ ] `frame_parser.h`의 중복 STX 정의 제거; `protocol_frame.h` 한 곳에서 제공.
- [ ] `protocol_frame.c`와 `uart_tx.c`의 `5U`를 공통 프레임 overhead 상수로 묶고 최대 프레임 크기도 같은 규격에서 산출.

## 3. 서식 — 동작 변경 없이 적용

- [ ] 함수 여는 중괄호를 다음 줄로 이동:
  `frame_parser.c`의 `parser_init`, `parser_feed`;
  `ring_buffer.c`의 `ring_init`, `ring_push`, `ring_pop`;
  `uart_rx.c`의 `uart_rx_init`, `uart_callback`.
- [ ] 제어문 괄호 앞 공백 추가:
  `frame_handler.c: update_track_from_measurement`의 `if(`;
  `uart_rx.c: uart_rx_init`의 `if(`, `uart_callback`의 `while(`;
  `rx_frame_processor.c: process_parser_result`의 `switch(`.
- [ ] `track.c: track_hit`의 switch 여는 중괄호를 같은 줄로 이동.
- [ ] `uart_rx.c: rx_recover_overflow`, `rx_frame_processor.c: process_parser_result`의 case 들여쓰기를 switch와 일치.
- [ ] 지역변수가 있는 case에 블록 추가:
  `frame_handler.c: handle_frame`의 `TYPE_STATUS` (`stats`),
  `frame_parser.c: parser_feed`의 `WAIT_CRC_L` (`valid`).
- [ ] 연산자 공백 정리: `frame_parser.c`의 `valid? ...:`와 `kalman_cv.c`의 `dt* dt`.
- [ ] 줄 끝 공백 제거: `frame_handler.c/.h`, `kalman_cv.c/.h`, `main.c`,
  `protocol_frame.c`, `rx_frame_processor.c`, `track.c`.
- [ ] 파일 끝 개행 추가: `angle_utils.h`, `crc16.h`, `frame_parser.c/.h`, `gating.c/.h`,
  `kalman_cv.h`, `protocol_frame.c/.h`, `radar_message.h`, `ring_buffer.c/.h`,
  `rx_frame_processor.c/.h`, `track.h`, `uart_tx.c/.h`.

## 4. 헤더·API 계약 — 문서 누락 보완

- [ ] 자신의 헤더를 첫 include로 이동: `frame_handler.c`, `protocol_frame.c`,
  `rx_frame_processor.c`, `uart_rx.c`, `uart_tx.c`. 표준 C·Zephyr·프로젝트 include를 그룹화.
  `gating.c`, `radar_message.c`, `kalman_cv.c`도 이후 그룹 사이 빈 줄/순서 정리.
- [ ] 직접 사용하지 않는 include 정리 후보:
  `main.c`의 device/uart, `uart_rx.c`의 string/frame_parser/frame_handler,
  `uart_tx.c`의 string/atomic, `rx_frame_processor.c`의 string, `uart_tx.h`의 stdbool.
  제거 후 빌드하여 숨은 전이 의존성도 확인.
- [ ] `protocol_frame.h` — validator는 현재 NULL·길이만 검사하고 TYPE whitelist는 검사하지 않음을 명시.
  CRC 함수에는 유효 프레임 전제, CRC 규격·계산 범위를 설명.
  encode에는 capacity 바이트 단위, 성공 바이트 수, errno, 실패 시 출력 유지 계약을 추가.
- [ ] `frame_handler.h`, `uart_tx.h` — submit의 큐 항목 복사, 큐 깊이 8, 비대기 정책,
  포화 시 신규 요청 거부, errno, 호출 문맥, 성공은 제출 완료라는 계약 추가.
- [ ] `frame_parser.h` — non-NULL 상태 객체, 단일 소유자, 결과 enum의 의미,
  FRAME_READY 후 frame 유효 기간(다음 feed 전 복사), init이 지우는 필드 범위 명시.
- [ ] `ring_buffer.h` — 포인터 전제, 실제 용량 255, full/empty의 false 반환,
  pop 실패 시 출력 유지, 동시 접근 보호 책임 명시. volatile만으로 안전하다고 설명하지 않기.
- [ ] `rx_frame_processor.h` — reset/feed/timeout은 RX 소유 문맥에서 호출,
  stats 출력은 non-NULL 전제, 필드별 조회이며 전체 동시 snapshot은 아님을 유지.
- [ ] `radar_message.h` — decode의 정확한 10바이트/바이트 순서, false 조건과 출력 유지,
  validate의 non-NULL 전제와 검사 범위를 명시. bool API를 기계적으로 errno로 바꾸지 않기.
- [ ] `angle_utils.h`, `gating.h`, `kalman_cv.h`, `track.h`, `crc16.h` — 단위·입력 전제·반환 의미 보완.
  각도 정규화의 유한 입력 전제와 반환 구간, filter의 dt 초 단위,
  track_predict 실패 시 상태를 지울 수 있는 조건, CRC 초기값/연속 update 방법 포함.
- [ ] `frame_handler.h`의 init/reset/check_timeout/handle_frame에 단일 소유 문맥과 유효 인자 전제 명시.

## 5. 주석·단위·const

- [ ] `uart_rx.c`의 rx_ready_sem 주석: handler 초기화까지 기다린다는 설명 제거.
  handler는 자기 스레드에서 초기화함.
- [ ] `uart_rx_thread()`의 IRQ 주석: 실제 보호 범위는 overflow 검사와 ring pop.
  parser feed는 unlock 후 실행됨. drain의 “tracking” 주석도 현재 파싱·큐 제출 책임에 맞춤.
- [ ] `frame_handler.h: last_valid_rx_ms` — handler에서 처리한 시각이라는 설명 수정.
  실제 값은 RX processor가 프레임 완성·제출 시 기록한 uptime이며 ISR 바이트 도착 시각도 아님.
  `received_frame.received_at_ms`에도 같은 기준 명시.
- [ ] `rx_frame_processor.h` timeout 주석에 마지막 byte 처리 시각 기준 1000ms, `>=` 만료,
  PARSER_STATE_WAIT_STX 제외 조건을 설명. reset이 last_byte_processed_ms를 지우지 않는 이유도 명시.
- [ ] `gating.c/.h`: measured/predicted range·range_error는 `_m`, azimuth·azimuth_error는 `_rad`.
  `angle_utils.c/.h`: angle/a/b에 `_rad`; `track.c/.h`: initial_range/measured_range에 `_m`, dt에 `_s`.
  `frame_handler.c`: predicted/filtered range·azimuth 지역변수에도 단위 표시.
- [ ] `kalman_cv.c/.h`: dt → dt_s, 각도 전용 인자 angle_measurement → angle_measurement_rad.
  거리/각도 공용 estimate/rate/value_measurement는 하나의 단위로 이름을 고정하지 않고 헤더에 모드별 단위 설명.
- [ ] `track_start()`의 거리 필터 p00/p11/q/R에도 단위와 선택 근거 추가.
  각도 필터에는 이미 분산 단위·표준편차 설명이 있음.
- [ ] 재대입하지 않는 지역 계산값을 const로: encoder의 length/frame_size/crc,
  processor의 result/received/ret, handler와 TX의 ret, TX length, RX의 IRQ key.
  반복 누적 CRC, parser/filter 상태, queue get 출력 request/frame은 대상에서 제외.
- [ ] 부동소수점 의도 명시: `angle_utils.c`의 `2 * PI_F`는 `2.0f`,
  `radar_message.c`의 range_m 비교는 `0.0f`. 현재 동작 오류라는 의미는 아님.

## 6. 정책 결정 후 적용 — 스타일 수정과 분리

- [ ] `protocol_frame_is_valid()`에서 enum→uint8 변환 범위 계약 결정.
  현재 enum에 0~255 밖 값을 넣으면 CRC/encode에서 축소됨.
  알 수 없는 1바이트 TYPE 허용 여부와 범위 밖 값 거부를 구분하고 테스트 추가.
- [ ] `ring_buffer.h`에 uint8 인덱스 크기 제약을 정적 assert로 추가 (`2 <= BUFFER_SIZE <= 256`).
  Zephyr 독립 모듈이므로 `_Static_assert`를 권장하고 디버그·릴리스 모두 유지.
  현재 256은 범위 내이며, 무조건 인덱스를 확대할 필요는 없음.
- [ ] `protocol_frame.h`의 최대 payload와 1바이트 길이 필드 제약도 정적 assert로 보호.
- [ ] parser/ring 등 내부 불변조건의 런타임 assert 적용 지점 선정.
  디버그에서만 활성화하도록 빌드 설정을 분리하고, 릴리스에서 CRC·길이·NULL 검증 및 errno 반환이 유지되는지 확인.
  조건식은 부작용 없이 작성하고 host 테스트의 판정용 assert는 계속 활성화.
- [ ] UART init 반복 호출·실패 후 submit 정책 명시. 현재 TX는 init 전에도 큐에 들어갈 수 있고,
  worker는 init 성공을 기다림. 재초기화 시 RX ring/parser를 재설정하는 영향도 결정.
- [ ] RX의 sem 대기, RX ISR의 uart_irq_update, handler/TX의 msgq_get 반환 처리 정책 정리.
  정상 timeout/영구 대기의 전제와 예상 밖 오류를 구분하고 무시하는 이유를 주석으로 기록.
  특히 callback 등록 실패의 원래 코드 보존은 1절에서 먼저 처리.
- [ ] 반복 로그 정책: handler의 매 측정 로그와 main alive, RX overflow 로그에
  디버그 레벨/출력 제한 적용 여부 결정. TX polling의 대기 특성과 완료 판정 범위를 문서화.
- [ ] 스레드 스택 1024/2048/1024, 우선순위 5/6/7, 큐 깊이 8의 근거와 측정 방법 기록.
  수치 변경이나 통계·새 스레드 추가는 별도 기능 작업으로 수행.

## 적용 순서와 완료 조건

1. errno·직접 include·잘못된 주석을 정리하고 관련 host 테스트/빌드 확인.
2. 이름·enum·서식 변경을 별도 묶음으로 적용. C 테스트와 Python C 래퍼의 심볼 참조도 함께 변경.
3. 공통 상수·계약·단위·const를 정리하고 경계 테스트 확인.
4. 6절 정책은 합의한 동작을 구현하고 새 회귀 테스트로 확인.

각 묶음마다 diff를 검토하고 해당 host 테스트와 펌웨어 빌드를 구분해 기록합니다.
이 점검은 소스 검토와 문서 정리이며, 새 테스트 실행·flash·실보드 검증 결과가 아닙니다.
