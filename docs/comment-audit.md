# 펌웨어 주석 점검 — 2026-11-14

범위: `fcc_app/src`의 C/헤더 30개. 주석 없는 줄 수가 아니라 API 계약, 단위, 소유권,
오류/상태 전이의 설명 유무를 점검했다. 영어 `//` 규칙을 유지한다.
단순 대입·자명한 함수는 헤더 계약으로 충분하며 줄마다 주석을 추가하지 않는다.
Python GUI/시뮬레이터와 외부 Zephyr 소스는 이 점검 범위에 포함하지 않는다.

## 발견하여 보완한 부분

| 파일 | 부족하거나 오래된 설명 → 반영 내용 |
|---|---|
| `tracking/gating.c`, `.h` | 고정 값·짧은 설명 → Kconfig 단위, inclusive half-width, AND, 최단 각도 차이, 필터 상태를 변경하지 않는 계약 |
| `common/math_constants.h` | 새 공통 π의 정밀도와 설정 대상이 아닌 이유 |
| `application/frame_handler.c` | Gating/Correction 표제 → 예측값 기준 검사와 gate miss/통신 오류 차이 |
| `application/frame_handler.h` | 고정 1000 ms 표현 → 설정값과 기본값 구분; COMMAND 오타 수정 |
| `transport/rx_frame_processor.h` | 고정 timeout 표현 → 설정값, 기본값, 바이트 처리 시각 기준 |
| `tracking/track.c` | 수명 정책 상수와 gate 설정의 독립성, 센서 timestamp 간격과 보드 무수신 timeout 차이 |
| `common/crc16.c` | CRC 한 바이트의 MSB-first/비반사 polynomial 연산 |
| `protocol/protocol_frame.c` | CRC가 포함/제외하는 wire 필드 |
| `transport/uart_tx.c` | 초기화 semaphore가 일회성 시작 대기라는 의미 |
| `transport/uart_rx.c` | 프로토콜 UART 역할, IRQ 활성화 전 parser 초기화; 의미 없는 error/경고 회피 표제 제거 |
| `main.c` | 콘솔과 USART6 프로토콜 분리, heartbeat 출력 혼합 가능성과 로깅 TODO |

## 나머지 파일 점검

| 파일 | 확인한 기존 설명 / 판단 |
|---|---|
| `common/crc16.h` | CRC 모델과 호출 계약 있음 |
| `common/ring_buffer.c`, `.h` | 한 슬롯 예약, 용량, 실패 반환 및 동기화 책임 설명 있음 |
| `tracking/angle_utils.c`, `.h` | fmodf 정규화 구간·유한값 전제·큰 float의 정밀도 한계 설명 있음; 공통 π 사용 |
| `tracking/kalman_cv.c`, `.h` | 상태/공분산 단위, 예측·보정 수식, innovation covariance 방어 계약 있음 |
| `tracking/track.h` | 상태 전이와 시간 정책 계약 있음 |
| `protocol/frame_parser.c`, `.h` | 길이·CRC·부분 수신 결과 및 parser 상태 설명 있음 |
| `protocol/radar_message.c`, `.h` | wire 필드 단위와 semicircle 변환 계약 있음; 공통 π 사용 |
| `protocol/protocol_frame.h` | wire 레이아웃, payload 크기, 반환값 계약 있음 |
| `protocol/status_message.c`, `.h` | 4바이트 big-endian 쓰기와 STATUS 카운터 레이아웃 계약 있음 |
| `transport/uart_rx.h` | 초기화/복구 정책 API 계약 있음 |
| `transport/uart_tx.h` | 복사 큐, 반환값, UART 송신과 상대 수신 보장의 차이 설명 있음 |
| `transport/rx_frame_processor.c` | parser 소유권·atomic 통계·timeout·완성 프레임 전달 설명 있음 |

현재 범위에서 추가로 반드시 보완해야 할 계약 주석은 발견하지 않았다.
알고리즘/정책을 변경할 때 이 판단도 다시 점검해야 한다. 특히 후속 로깅 작업에서는
heartbeat 임시 설명을 실제 정책으로 갱신하고, 다중표적 작업에서는 단일 RX/handler 소유권과
고정 직사각형 게이트 설명을 함께 검토한다.
