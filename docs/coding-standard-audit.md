# 코딩 스탠다드 적용 점검

2026-11-10 `fcc_app/src` C·헤더 27개에 대한 적용 결과입니다.
[코딩 스탠다드](coding-standard.md)에 맞춰 이름·서식·의존성·주석을 정리했습니다.
초기 스타일 정리에서는 assert를 제외했으며, 이후 적용 상태는 아래 assert 항목을 따릅니다.
테스트 코드는 참조 변경과 검증에 필요한 부분만 수정했습니다.

## 완료된 항목

### 오류 반환·API 계약

- [x] RX/TX 장치 미준비는 `-ENODEV`, callback 등록 실패는 원래 음수 errno 전달.
- [x] main에서 오류 코드를 출력하고 그대로 반환. callback 등록 로그 자체에는 코드를 중복 출력하지 않음.
- [x] RX host 테스트에 미준비·등록 실패(-ENOSYS/-ENOTSUP)·성공 경로 추가.
  실패 시 IRQ 활성화와 준비 신호가 발생하지 않는지 확인. TX도 정확한 `-ENODEV` 확인.
- [x] 공개 헤더에 입력 전제·단위·반환값·출력 유지·복사 수명·상태 소유권 설명.
- [x] RX init은 시작 시 한 번 호출, TX는 반복 호출 시 준비 신호만 전달하고 큐는 유지함을 문서화.

### 이름

| 위치 | 적용한 이름 |
| --- | --- |
| protocol_frame_encode 출력 | `out_buffer` |
| decode_radar_measurement 출력 | `out_measurement` |
| ring_pop 출력 | `out_byte` |
| rx_frame_processor_get_stats 출력 | `out_stats` |
| handle_frame / handle_radar_measurement / uart_tx_submit | 선언·정의 모두 `p_frame` |
| FrameType / ParserState / ParserResult | `FRAME_TYPE_*` / `PARSER_STATE_*` / `PARSER_RESULT_*` |
| TrackState / RxOverflowPolicy | `TRACK_STATE_*` / `RX_OVERFLOW_POLICY_*` |

- [x] 상태 객체 참조는 `p_`, 별도 출력은 `out_` 유지.
- [x] 거리·각도 전용 매개변수와 지역변수에 `_m`·`_rad`, dt에 `_s` 적용.
  거리/각도 공용 Kalman 값은 공용 이름을 유지하고 단위를 주석으로 설명.
- [x] wire 값·기존 로그 문자열·상태 전이 순서는 유지.

### 공통 규격·헤더

- [x] `PROTOCOL_STX`, `PROTOCOL_MAX_PAYLOAD`, `RING_BUFFER_SIZE`, `CRC16_INITIAL_VALUE`로 상수명 구체화.
- [x] STX 중복 정의 제거. `PROTOCOL_FRAME_OVERHEAD`, `PROTOCOL_MAX_FRAME_SIZE`를 프로토콜 헤더에서 제공.
- [x] 자신의 공개 헤더를 첫 include로 배치하고 표준 C·Zephyr·프로젝트 헤더 그룹 구분.
- [x] errno/printk 헤더 직접 포함, 불필요 string/atomic/device/uart/프로젝트 include 제거.
- [x] host fixture의 명시적 의존성과 TX printk 모사 헤더, Python C 래퍼 상수 참조 갱신.

### RX/handler·TX 공통 프레임과 검증

이 리팩터링은 구현 완료 상태이며 README의 미완료 표기를 수정했습니다.

| 책임 | 현재 구현 |
|---|---|
| 공통 데이터 | [`protocol_frame.h`](../fcc_app/src/protocol/protocol_frame.h)의 `struct protocol_frame`: TYPE·payload 길이·payload |
| 공통 기본 검증 | [`protocol_frame.c`](../fcc_app/src/protocol/protocol_frame.c)의 `protocol_frame_is_valid()`: NULL 및 payload 길이 1..32 |
| RX 전용 메타데이터 | [`frame_handler.h`](../fcc_app/src/application/frame_handler.h)의 `received_frame.received_at_ms`: parser 완료/제출 시점의 보드 uptime |
| RX 제출 | [`frame_handler_submit()`](../fcc_app/src/application/frame_handler.c): 바깥 포인터 NULL 검사 → 공통 검증 → `frame_queue`에 복사 |
| TX 제출 | [`uart_tx_submit()`](../fcc_app/src/transport/uart_tx.c): 바깥 포인터 NULL 검사 → 공통 검증 → `tx_queue`에 복사 |

두 submit은 무대기 제출이며 잘못된 인자는 `-EINVAL`, 큐 포화는 `-ENOMSG`를 반환합니다.
바깥 구조체의 NULL 검사는 `&p_frame->frame`에 접근하기 전에 필요하므로 각 API에 유지합니다.
이 짧은 검사와 큐 호출을 합치기 위한 `void *` 공통 submit은 만들지 않습니다.
`send_frame`은 현재 공통 프레임만 감싸지만 TX 요청 타입으로 유지할 수 있습니다.
이를 제거하는 것은 선택적 API 단순화이며 이 공통화 작업의 미완료 조건이 아닙니다.

기본 검증은 TYPE 지원 여부·payload 의미·수신 CRC까지 검증하는 함수가 아닙니다.
CRC/길이 조립 검증은 parser, 측정 해석·유효성 검사는 handler 경로에 남깁니다.
TYPE이 wire의 1바이트 범위 밖일 때 명시적으로 거부할지는 아래 별도 정책 과제입니다.
수신 시각은 payload의 센서 timestamp와 별개이며 TX 구조체에는 추가하지 않습니다.

### 서식·주석·const

- [x] 함수 중괄호는 다음 줄, 제어문은 같은 줄. 제어문 괄호 앞 공백과 case 들여쓰기 통일.
- [x] 지역변수가 있는 STATUS_REQUEST/WAIT_CRC_L case에 블록 적용.
- [x] 삼항/곱셈 연산자 공백, 줄 끝 공백, 불필요 연속 빈 줄, 파일 끝 개행 정리.
- [x] 불변 지역 계산값·제출 프레임·반환값·IRQ key에 const 적용.
- [x] 각도 배수와 거리 0 비교의 float 리터럴 명시.
- [x] 영어 `//` 주석, 파일 헤더에만 블록 주석 허용 규칙 반영.
- [x] RX 준비 신호·IRQ 보호 범위·프레임 제출 시각 설명 수정.
- [x] CRC 범위·파서 결과 수명·링 용량·필터 단위·초기 분산 설명 보완.

## assert 후속 적용 (2026-11-14)

- [x] 링 인덱스와 버퍼 크기, wire 길이 제약의 정적 assert.
- [x] 내부 불변조건의 디버그 전용 런타임 assert와 빌드 설정.

일반 펌웨어는 assert 비활성, `debug.conf`는 활성입니다.
host 테스트는 공통 어댑터로 assert를 항상 검사합니다.

## 코딩 표기와 별개인 후속 동작·검증

이 항목들은 스타일 정리로 동작을 바꾸지 않기 위해 별도 작업으로 남깁니다.

- [ ] enum TYPE이 1바이트 범위 밖인 경우의 명시적 거부 정책.
  현재 CRC/encode는 한 바이트 TYPE을 전제로 하며 validator는 NULL·길이만 확인.
- [ ] UART 재초기화와 init 실패 후 큐 처리 정책 재검토.
  현재 TX는 init 성공까지 대기하고 재호출 시 큐를 유지한다. 기존 계약은 문서화됐으며,
  재초기화 때 큐를 비울지·init 실패 이후 제출을 거부할지는 별도 정책 결정이다.
- [ ] uart_irq_update 및 큐/세마포어의 예상 밖 실패에 대한 추가 복구 정책.
  RX 50ms 대기 만료는 정상 제어 흐름임을 주석으로 명시.
- [x] RX 큐 드롭 로그는 첫 발생 및 매 100회로 출력 제한.
- [ ] 전체 로그의 모듈·레벨·출력 제한과 버퍼 유실 정책, TX 완료/실패 통계.
- [ ] 스레드 스택 실측. 현재 RX/TX 1024, handler 2048바이트와 우선순위 5/7/6,
  큐 깊이 8은 기존 설정을 유지하며 충분성을 이번 host 테스트로 입증하지 않음.
  실보드에서 최악 입력·오류 로그 부하를 걸고 스택 사용량을 측정한 후 조정.
- [x] STATUS_REPORT payload·TX 큐 제출·응답 인코딩 구현 및 host 검증.
- [x] PC STATUS 파서·CLI·GUI 수동 조회 구현과 Python 모사 검증.
- [x] 실제 보드 STATUS TX 기본 왕복 — `2aa57d9`, CLI·GUI·오류 통계·추적 병행 확인.
- [x] TX host에서 큐 복사·FIFO·포화 후 재제출 및 인코딩 실패 후 다음 요청 처리 검증.
  근거: `test_tx_queue_copy_fifo_full_and_recovery`, `test_tx_encode_error_continues`;
  STATUS 제출 실패 복귀는 `test_status_submit_failure_and_recovery`에서 검증한다.
- [x] RX/TX 데이터 선 물리 단절·복원: 무수신/응답 timeout 및 재부팅 없는 복귀.
  [실보드 증거](hardware-test-2026-11-14-link-loss.md).
- [ ] 실제 TX 포화·최대 부하 및 USB 제거·전원 재인가 등 추가 장애 복구 검증.

## 검증 범위

RX/STATUS/TX/Tracking host 테스트, Python 전체 테스트, STM32 펌웨어 빌드를 확인합니다.
상세 결과는 [테스트 목록](test-cases.md)의 최신 기록을 따릅니다.
flash·실보드 동시성·물리 UART 성공은 이번 정리의 검증 범위에 포함하지 않습니다.

2026-11-14: 위 초기 제외 기록 이후 사용자가 펌웨어 assert를 추가했습니다.
일반 빌드는 assert 비활성, debug.conf는 활성화하며 host 테스트는 공통 어댑터로 항상 검사합니다.

## 주석 점검 갱신

2026-11-14: [펌웨어 31개 파일 주석 점검](comment-audit.md).
게이트 설정·단위·경계와 timeout/CRC/스레드 시작 계약을 보완했다.
