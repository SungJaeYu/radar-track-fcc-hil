# radar-track-fcc-hil

## 프로젝트 정체성

STM32 + Zephyr 기반 레이더 추적 학습 프로젝트.
현재는 PC 측정 송신 → STM32 단일 트랙 처리 → STATUS/TRACK 이진 보고 송신 구조입니다. PC는 STATUS/TRACK을 해석하고 GUI에 보드 추정값을 표시합니다. 원시 로그·정량 평가는 미구현입니다. ST-Link는 별도 진단 콘솔입니다.
단일표적 결과 반환·정량 검증은 아직 미완료입니다. v1의 범위와 완료 판정은 [README v1 검증 및 완료 기준](README.md#v1-acceptance)을 따릅니다. 다중표적·CT/IMM·BIT·Lock On은 v2 선택이며 v1 완료를 미루는 필수 작업으로 복원하지 않습니다. 계획 변경을 펌웨어 구현·시험 완료로 표시하지 않습니다.

## 개발 환경

- 보드: STM32F746G-DISCO, 보드 ID `stm32f746g_disco`.
- west workspace: `~/zephyrproject`, Python venv: `~/zephyrproject/.venv`.
- 작업 전 `source ~/zephyrproject/.venv/bin/activate`.
- 빌드: `west build -d <repo>/build -b stm32f746g_disco <repo>/fcc_app -- -DCMAKE_EXPORT_COMPILE_COMMANDS=ON`.
- flash: `west flash -d <repo>/build --runner openocd` (openocd 러너 사용).

## 현재 구조와 규격

- PC 도구는 `pc_sim/` 하나. 모든 송신기가 `pc_sim/protocol.py`를 공유합니다.
- `radar_sender.py`: 거리 변화율 기반 송신. `run_sim.py`: 표적·센서 시나리오.
- `frame_sender.py`: 수동 INIT 등 단일 프레임 송신.
- `python -m pc_sim.gui`: PySide6/pyqtgraph 단일표적 GUI. 설정·측정 생성·시리얼 작업은 `gui_model.py`·`gui_worker.py`로 분리합니다.
- GUI는 PC 합성 실제값·측정값과 수신한 보드 추정값을 구분합니다. 미리보기에서는 보드 값을 만들지 않습니다. 생성/송신 횟수는 수신 ACK가 아닙니다.
- 프레임: `7E | LEN | TYPE | PAYLOAD | CRC_H | CRC_L`.
- 측정 TYPE=2, payload `>IHi` 10바이트: uint32 ms, uint16 m, int32 semicircle.
- 보드 `measurement_message.c`가 semicircle을 rad로 변환합니다.
- RX 스레드는 `rx_frame_processor`로 파싱하고 완성 프레임을 큐에 복사합니다.
- 별도 handler 스레드에서 `frame_handler`는 큐·TYPE 분기, `radar_processor`는 측정·추적·무수신 감시, `report_sender`는 STATUS/TRACK 구성·제출을 담당합니다. 스레드를 추가한 분리가 아닙니다.
- STATUS는 processor의 네 통계에 UART의 세 통계를 더한 일곱 값을 28바이트 STATUS_REPORT로 TX 큐에 복사 제출합니다. 각 값은 atomic 조회하며 동시 snapshot은 보장하지 않습니다.
- TX 초기화·복사 큐·워커·인코딩·polling 송신은 구현되어 있습니다. STATUS 제출까지 연결했으며 PC 파서·STATUS CLI·GUI 수동 조회까지 구현했으며 [확장 STATUS 실보드 시험](docs/hardware-test-2026-11-14-status-extended.md)에서 28바이트 응답의 CLI·GUI 왕복도 확인했습니다. 강제 링 overflow·UART 하드웨어 오류·지속 부하 검증은 남아 있습니다.
- RX 링버퍼는 256바이트, 실제 저장 용량은 255바이트입니다. 인덱스는 `uint8_t`이므로 더 큰 버퍼는 타입 변경이 필요합니다.
- overflow 정책은 `fcc_app/Kconfig` choice와 `prj.conf`에서 선택하며 `uart_rx.c`에서 매핑합니다. 기본값은 DRAIN입니다.
- overflow 복구 중 새 입력은 버리고 저장된 바이트로 완성 가능한 프레임만 처리한 뒤 링·파서를 초기화합니다.
- 단일 트랙, CV Kalman, 게이팅, hit/miss 상태 전이, 정상 측정 수신 timeout이 있습니다.
- TRACK_REPORT(TYPE=7)는 32바이트 payload이며 유효 측정마다 한 번 TX 제출합니다. PC 해석·GUI의 2초 STALE 정책은 구현했으며 제어/timeout 이벤트 보고는 미구현입니다. 상세 계약은 [SPEC-TRACK-REPORT](docs/requirements-traceability.md#spec-track-report)를 따릅니다.
- 다중표적 데이터 연관·LCD는 현재 실행 경로에 없습니다.
- 기존 XOR 인코더는 CRC-16/IBM-3740으로 변경했습니다. 과거 구현은 Git 이력에 남아 있습니다.

## 검증

- `python -m pytest`: GUI 의존성 설치 시 221개 + subtest 9개. 미설치 시 Qt 테스트는 skip됩니다. 실제 보드와 별도입니다.
- [UART host 테스트](fcc_app/tests/test_uart_rx/README.md): drain 24·discard 21, 총 45회 실행으로 링버퍼·부분 프레임 timeout·통계·init 오류를 검증합니다. 모사 IRQ는 실제 동시성을 검증하지 않습니다.
- [handler host 테스트](fcc_app/tests/test_frame_handler/README.md): STATUS·TRACK·실패 복귀·INIT/RESET·측정 timeout·processor 계약 29개 사례. 통계는 모사 snapshot이며 실제 RX 카운터 통합은 별도입니다.
- [TX host 테스트](fcc_app/tests/test_uart_tx/README.md): 초기화·요청 검증·복사 큐·송신 순서·실패 후 복귀 4개 사례.
- [Tracking C 테스트](fcc_app/tests/test_tracking/README.md): 필터·게이팅·추적 정책 17개 + 게이트 설정 행렬 20개 사례.
- 수동 초기화만 필요할 때: `python pc_sim/frame_sender.py init --type 1 --port PORT`.
- 송신: `python pc_sim/radar_sender.py --port PORT --count 20 --noise 0`.
- 두 연속 송신기는 실행마다 INIT을 한 번 자동 전송하고 timestamp를 0부터 시작합니다.
- 보드 수신·추적 결과는 ST-Link 콘솔에서 확인합니다.
- Python/host 테스트, 펌웨어 빌드, flash, 실제 보드 HIL을 구분해서 보고합니다.

## 작업 원칙

- 공개 문서는 프로젝트의 문제·구조·구현 상태·검증 결과·한계를 중심으로 작성합니다. 개인의 구직 계획이나 다음 프로젝트로 이동하는 일정은 공개 기술 문서에 포함하지 않습니다.

- [코딩 스탠다드](docs/coding-standard.md): 참조 포인터 `p_`, 출력 `out_`, enum PascalCase와 타입명 기반 열거자 접두사, 함수 중괄호 다음 줄, 제어문 같은 줄·공백 한 칸. 음수 errno 반환 규칙을 적용했으며 기존 펌웨어를 임의로 일괄 수정하지 않습니다.

- 작은 검증 사다리로 진행. 각 단계는 직전 단계 위에서 검증 가능해야 함.
  매 단계 끝에 "어떻게 보드에서 확인하는지"를 명시할 것.
- 한 번에 다 만들지 말 것. 골격 먼저, 살은 단계별로.
- 코드 주석은 영어 `//`로 작성합니다. 파일 헤더 주석은 `/* */`도 허용합니다. 설명 문서는 한국어로 작성할 수 있습니다.
- 펌웨어를 직접 작성하며 학습하는 방식을 존중합니다. 설명·검토 요청만으로 펌웨어를 수정하지 않습니다.
- 인증 아티팩트(DO-178C 산출물 등)를 코드에서 자동 생성하지 말 것.
  그건 별도 트랙이고, 코드 자동생성은 안티패턴.

## 기준 문서와 검증 경계

- 현재 규격·테스트 연결: [요구사항 추적표](docs/requirements-traceability.md).
- 과거 빌드·실보드 결과: [검증 이력](docs/verification-history.md). 대상 commit과 범위를 함께 읽습니다.
- 2026-11-16 `87006b0`: TRACK_REPORT·processor 분리 host 29개, 일반/디버그 빌드 확인. 새 결과 경로의 flash·PC 왕복은 미검증입니다.
- 게이트/timeout/overflow는 `fcc_app/Kconfig`·`prj.conf`, 로그/디버그는 [로깅 안내](docs/logging.md)를 따릅니다. π는 `common/math_constants.h`의 상수입니다.
- 2026-11-17 파일·API·변수 이름 변경을 적용했습니다. [변경 전후와 남은 설계 과제](docs/naming-review.md)를 참고합니다.
- 과거 `docs/test-results/` 스크립트는 당시 revision용입니다. 현재 API로 재사용할 때 호환성을 확인하며 원시 증거를 덮어쓰지 않습니다.
