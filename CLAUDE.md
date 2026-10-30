# radar-track-fcc-hil

## 프로젝트 정체성

STM32 + Zephyr 기반 레이더 추적 학습 프로젝트.
현재는 PC 측정 송신 → STM32 단일 트랙 처리 → ST-Link 콘솔 확인 구조입니다.
다중표적·폐루프 정량 검증은 목표이며 현재 동작으로 단정하지 않습니다.

## 개발 환경

- 보드: STM32F746G-DISCO, 보드 ID `stm32f746g_disco`.
- west workspace: `~/zephyrproject`, Python venv: `~/zephyrproject/.venv`.
- 작업 전 `source ~/zephyrproject/.venv/bin/activate`.
- 빌드: `west build -p always -b stm32f746g_disco <app_path>`.
- flash: `west flash --runner openocd` (openocd 러너 사용).

## 현재 구조와 규격

- PC 도구는 `pc_sim/` 하나. 모든 송신기가 `pc_sim/protocol.py`를 공유합니다.
- `radar_sender.py`: 거리 변화율 기반 송신. `run_sim.py`: 표적·센서 시나리오.
- `small_byte_test.py`: 수동 INIT 등 단일 프레임 송신.
- 프레임: `7E | LEN | TYPE | PAYLOAD | XOR(TYPE, PAYLOAD)`.
- 측정 TYPE=2, payload `>IHi` 10바이트: uint32 ms, uint16 m, int32 semicircle.
- 보드 `radar_message.c`가 semicircle을 rad로 변환합니다.
- `uart_rx` 스레드가 파싱·handler·track 처리를 수행합니다.
- RX 링버퍼는 256바이트, 실제 저장 용량은 255바이트입니다. 인덱스는 `uint8_t`이므로 더 큰 버퍼는 타입 변경이 필요합니다.
- `uart_rx.h`의 `RX_OVERFLOW_POLICY` 기본값은 `RX_OVERFLOW_DRAIN_BUFFER`입니다. Kconfig는 아직 없습니다.
- overflow 복구 중 새 입력은 버리고 저장된 바이트로 완성 가능한 프레임만 처리한 뒤 링·파서를 초기화합니다.
- 단일 트랙, CV Kalman, 게이팅, hit/miss 상태 전이, 정상 측정 수신 timeout이 있습니다.
- 다중표적 데이터 연관·이진 트랙 응답·LCD는 현재 실행 경로에 없습니다.
- 기존 CRC16 코덱과 구버전 설계 문서는 제거했습니다. 과거 구현은 Git 이력에 남아 있습니다.

## 검증

- `python -m pytest`: PC 테스트. 실제 보드와 별도입니다.
- [UART host 테스트](fcc_app/tests/test_uart_rx/README.md): drain 복구·링버퍼 8개 사례. 모사 IRQ는 실제 동시성을 검증하지 않습니다.
- [Tracking C 테스트](fcc_app/tests/test_kalman/README.md): 필터·게이팅·추적 정책 13개 사례.
- 수동 초기화만 필요할 때: `python pc_sim/small_byte_test.py init --type 1 --port PORT`.
- 송신: `python pc_sim/radar_sender.py --port PORT --count 20 --noise 0`.
- 두 연속 송신기는 실행마다 INIT을 한 번 자동 전송하고 timestamp를 0부터 시작합니다.
- 보드 수신·추적 결과는 ST-Link 콘솔에서 확인합니다.
- Python/host 테스트, 펌웨어 빌드, flash, 실제 보드 HIL을 구분해서 보고합니다.

## 작업 원칙

- 작은 검증 사다리로 진행. 각 단계는 직전 단계 위에서 검증 가능해야 함.
  매 단계 끝에 "어떻게 보드에서 확인하는지"를 명시할 것.
- 한 번에 다 만들지 말 것. 골격 먼저, 살은 단계별로.
- 코드 주석은 영어 또는 영어·한국어 병기로 작성합니다. 설명 문서는 한국어로 작성할 수 있습니다.
- 펌웨어를 직접 작성하며 학습하는 방식을 존중합니다. 설명·검토 요청만으로 펌웨어를 수정하지 않습니다.
- 인증 아티팩트(DO-178C 산출물 등)를 코드에서 자동 생성하지 말 것.
  그건 별도 트랙이고, 코드 자동생성은 안티패턴.
