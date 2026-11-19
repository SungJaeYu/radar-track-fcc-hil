# 보드 연결·빌드·기본 검증

[문서 목차](README.md) · [PC CLI](../pc_sim/README.md) · [GUI 사용법](../pc_sim/GUI.md)

STM32F746G-DISCO의 USART6를 측정 입력·STATUS/TRACK 결과 출력에 사용합니다.
ST-Link USB는 플래시와 진단 콘솔을 담당합니다. 아래 순서로 연결·빌드·기본 왕복을 확인하세요.

## 배선과 통신 설정

기준은 `fcc_app/boards/stm32f746g_disco.overlay`입니다.
**USART6 TX=PC6, RX=PC7, 115200 baud, 8N1, 흐름제어 없음**을 사용합니다.
3.3V USB-UART 어댑터를 사용하고 보드의 실제 핀 위치를 확인합니다.

| USB-UART | STM32 핀 | 역할 |
| --- | --- | --- |
| TXD | PC7 / USART6_RX | PC 측정·제어 요청 입력 |
| RXD | PC6 / USART6_TX | STATUS_REPORT·TRACK_REPORT 출력; PC 디코더·GUI 지원; TRACK 기본 실보드 왕복 확인 |
| GND | GND | 공통 접지 |
| VCC / 5V / 3V3 | 연결하지 않음 | 보드와 어댑터는 각자 USB로 급전 |

TX→RX, RX←TX로 교차 연결합니다. 핀 변경 시 overlay도 함께 변경합니다.
ST-Link USB는 flash와 Zephyr 콘솔 로그에 사용하며 데이터용 USB-UART와 구분합니다.
프레임·TYPE·payload 규격은 [PC 도구 문서](../pc_sim/README.md)를 참고합니다.

## 1. 빌드·플래시

Zephyr4.2·SDK0.17.2와 Python 환경을 준비한 뒤 실행합니다. `/path/to/radar-track-fcc-hil`은 실제 저장소 경로로 바꿉니다.

```bash
source ~/zephyrproject/.venv/bin/activate
cd ~/zephyrproject
west build -d /path/to/radar-track-fcc-hil/build -b stm32f746g_disco /path/to/radar-track-fcc-hil/fcc_app
west flash -d /path/to/radar-track-fcc-hil/build --runner openocd
```

빌드 성공, flash 성공, 보드 실행 성공을 각각 구분해서 기록합니다.

## 2. 포트와 콘솔

- ST-Link VCP: 부팅·측정·추적 로그. `=== FCC learning-rebuild boot ===` 확인.
- 외부 USB-UART: USART6 측정·제어 입력과 STATUS/TRACK 보고 출력. 위 배선 참조.
- 하나의 데이터 포트를 송신기와 모니터가 동시에 열지 않도록 합니다.
- Linux 예: 콘솔 `/dev/ttyACM0`, 데이터 `/dev/ttyUSB0`.
  macOS는 각각 `cu.usbmodem...`, `cu.usbserial...` 등의 실제 장치명을 확인합니다.

일반 빌드는 INFO 이상만 출력합니다. 아래 측정·예측 로그를 확인하는 절차는
`debug.conf` 빌드가 필요합니다. STATUS의 여덟 통계는 일반 빌드에서도 wire로 조회합니다.
[로깅 빌드 방법](logging.md).

## 3. 자동 INIT과 측정 송신

저장소 루트에서:

```bash
python pc_sim/radar_sender.py --port /dev/ttyUSB0 --count 20 --hz 10 --noise 0
```

- 콘솔에서 `INIT: track and measurement monitoring reset`과 최초 `Track started` 로그 확인.
- 이어지는 예측·측정 처리 로그 확인. 거리와 각도 단위를 구분합니다.
- PC 송신 출력은 보드 수신 증거가 아닙니다.
- 송신 중단 후 약 1초가 지나면 timeout 로그가 한 번 나오고 트랙이 초기화되어야 합니다.
- 부팅 직후 또는 INIT 직후 첫 정상 측정 전에는 timeout 로그가 나오지 않는 것이 현재 정책입니다.

## 4. 시나리오

시나리오 실행기도 시작 시 INIT을 자동 전송합니다:

```bash
python -m pc_sim.run_sim --port /dev/ttyUSB0 --duration 10 --noise 0 --azimuth-noise 0
```

기본값은 단일 접근 표적입니다. 다중표적·클러터는 PC 출력 전용이며 UART로 보내지 않습니다.
재실행 시 INIT이 자동 전송되므로 보드를 재부팅하거나 별도로 수동 INIT을 보낼 필요가 없습니다.
이진 응답 수집·RMSE는 `python -m pc_sim.evaluate run --port /dev/ttyUSB0 --scenario noisy`를 사용합니다. run_sim은 송신 전용입니다.

## 5. Overflow 복구 재현

기본 DRAIN의 강제 overflow와 정상 복귀는 [전송 오류 시험](hardware-test-2026-11-18-transport-stress.md)에서 확인했습니다. 아래는 재현 시 확인할 항목입니다.

현재 소프트웨어 RX 버퍼는 256바이트(저장 용량 255바이트), 복구 정책은 drain입니다.
[UART host 테스트](../fcc_app/tests/test_uart_rx/README.md)는 보드 없이 수행하며,
아래 절차의 실제 하드웨어 검증과 구분합니다.

1. 정상 속도 송신으로 INIT·측정 처리를 먼저 확인합니다.
2. 수신 스레드가 처리하는 속도보다 많은 바이트가 쌓이도록 burst 입력 등으로 부하를 줍니다.
   `--hz`를 높이는 것만으로 overflow 발생을 보장하지는 않습니다. UART 전송 속도와 스레드 처리량에 영향을 받습니다.
3. ST-Link에서 `RX overflow: recovery completed` 로그로 소프트웨어 overflow 복구 경로 실행을 확인합니다.
4. 부하를 낮추고 새 정상 프레임의 처리가 재개되는지 확인합니다. 필요하면 INIT을 보내 추적 상태를 다시 시작합니다.
5. 송신 중단 후 timeout도 별도로 확인합니다. 첫 정상 측정 전에는 감시하지 않습니다.

소프트웨어 링버퍼 크기는 하드웨어 FIFO 크기와 다릅니다. drain 복구 중 새 입력은 버립니다.
복구 로그만으로 무손실 수신이나 다음 프레임의 즉시 처리를 판정하지 않습니다.
STATUS의 overflow 횟수·ISR 폐기 바이트·복구 폐기 바이트를 시험 전후 비교합니다.
하드웨어 유실과 parser가 소비한 부분 프레임은 제외하므로 전체 손실량으로 해석하지 않습니다.
하드웨어 UART overrun은 소프트웨어 링버퍼 overflow와 별도 확인 대상입니다.

## 6. 검증 기록

Python 테스트, UART/Tracking host C 테스트, 펌웨어 빌드, flash, 실제 보드 로그를 별도 결과로 남깁니다.
통합 도구의 테스트 통과만으로 새 보드 HIL까지 통과했다고 기록하지 않습니다.

STATUS 왕복 결과: [확장 STATUS 실보드 결과](hardware-test-2026-11-14-status-extended.md). 이후 로그 부하 시험과 최신 host/build 결과는 [검증 기록](verification-history.md)을 따릅니다. [TRACK 실보드](hardware-test-2026-11-18-track.md)와 [CSV·평가·GUI](hardware-test-2026-11-18-evaluation.md)를 확인했습니다. 이전 측정·GUI 기록은 [2026-11-01 검증](hardware-test-2026-11-01.md)에 보존합니다.

### STATUS 통계 확인 (기본 왕복 검증 완료, 재현 절차)

```sh
python -m pc_sim.status_client --port /dev/ttyUSB0 --timeout 2
```

현재 STATUS_REQUEST payload 내용은 사용하지 않지만 프레임의 LEN은 1..32여야 합니다.
통계는 CLI/GUI의 이진 응답으로 확인합니다. 디버그 콘솔의 `STATUS report queued`는 큐 접수만 의미합니다.
CRC 오류·잘못된 길이·부분 프레임 중단을 주입한 뒤 재조회하고, INIT/RESET 후에도
누적값이 유지되는지 확인합니다. STATUS만 보내도 측정 무수신 timeout은 계속 동작해야 합니다.
펌웨어는 통계를 32바이트 STATUS_REPORT로 구성해 TX 큐에 복사 제출합니다.
CLI는 응답 TYPE·길이·CRC를 검사한 뒤 여덟 통계를 출력합니다.
GUI·다른 송신기를 닫아 포트를 단독 사용하고 PC6 TX → 어댑터 RXD 배선도 확인합니다.
정상 조회·반복 조회·오류 주입 후 통계 증가·추적 중 조회를 확인하고 로그와 펌웨어 버전을 남깁니다.
GUI 시험은 CLI 종료 후 STATUS 탭에서 별도로 수행합니다.
`2aa57d9`에서 이 기본 절차를 수행했습니다. 상세 범위와 로그는 [검증 이력](verification-history.md)을 참고합니다.

## TRACK_REPORT 추가 후 검증 경계

현재 펌웨어는 신선한 유효 측정 payload마다 TYPE=7을 제출합니다. STATUS CLI는 다른 TYPE을 건너뛰고,
GUI worker와 evaluate CLI는 TRACK 결과와 원시 wire CSV를 저장합니다. 다만 기존 STATUS 명령 성공만으로
측정 결과 반환을 검증했다고 판정하지 않습니다.

지속 부하·오류 복귀 후속 시험에서는 [TRACK_REPORT 규격](requirements-traceability.md#spec-track-report)에 맞는
구현된 PC 디코더·GUI·CSV 수집기로, 값·시각·순번·유효성·오류 후 복귀를 확인해야 합니다.
[로그 부하 실행기](../fcc_app/tests/log_stress/README.md)는 STATUS/TRACK 혼합의 CRC·길이를 검사하도록 수정했습니다. TRACK 수치·순번·전달률을 평가하는 도구는 아니며 수정 후 실보드 재시험은 남아 있습니다.

날짜별 Kconfig·각도·게이트·STATUS·로깅 시험은 [검증 이력](verification-history.md)에 모았습니다.
각 기록의 commit과 당시 미검증 범위를 보존하며 최신 펌웨어 전체의 합격 근거로 확대하지 않습니다.

## 주요 설정

애플리케이션 설정은 `fcc_app/Kconfig`에서 정의하고 `fcc_app/prj.conf`에서 선택합니다.
변경 후 펌웨어를 다시 빌드합니다.

| 설정 | 기본값 | 의미 |
|---|---|---|
| `CONFIG_RADAR_RX_OVERFLOW_DRAIN` | `y` | 저장된 바이트로 완성 가능한 프레임을 처리한 뒤 복구 |
| `CONFIG_RADAR_FRAME_RX_TIMEOUT_MS` | `1000` | 부분 프레임 바이트 처리 중단 제한(ms) |
| `CONFIG_RADAR_MEASUREMENT_TIMEOUT_MS` | `1000` | 첫 정상 측정 이후 무수신 제한 및 큐 측정 최대 허용 나이(ms, 경계 폐기) |
| `CONFIG_RADAR_RANGE_GATE_M` | `100` | 거리 오차 상한(m) |
| `CONFIG_RADAR_AZIMUTH_GATE_DEG` | `10` | 각도 오차 상한(정수 degree) |

게이트 설정 예:

```ini
CONFIG_RADAR_RANGE_GATE_M=100
CONFIG_RADAR_AZIMUTH_GATE_DEG=10
```

값은 전체 창 폭이 아닌 **허용 오차의 절댓값 상한**입니다. 기본값은 ±100 m·±10°이며,
두 조건을 모두 만족해야 통과합니다. 경계도 포함합니다. 거리 설정은 1–65535 m,
각도 설정은 정수 1–180°를 허용하고 내부 계산은 radian입니다.
180°는 정규화된 각도 오차를 모두 허용하지만 거리 검사는 유지합니다.
공분산 기반 적응형 게이트는 아닙니다. π는 `common/math_constants.h`의 수학 상수입니다.

[설정별 테스트](../fcc_app/tests/test_tracking/README.md) · [전체 펌웨어 주석 점검](comment-audit.md).
