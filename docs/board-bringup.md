# 현재 학습 펌웨어 배선·보드 검증

현재 실행 경로는 USART6 수신 → rx_frame_processor → 메시지 큐 → handler 스레드 → ST-Link 로그입니다.
과거 LCD 구현 문서와 달리 현재 소스에는 LCD 표시나 이진 트랙 응답 경로가 없습니다.

STATUS 요청에는 handler → TX 복사 큐 → TX 스레드 → USART6 응답 경로도 있습니다.
PC 응답 디코더·CLI·GUI는 구현했습니다. STATUS 기본 실보드 왕복은 `2aa57d9`에서 확인했습니다.

## 배선과 통신 설정

기준은 `fcc_app/boards/stm32f746g_disco.overlay`입니다.
**USART6 TX=PC6, RX=PC7, 115200 baud, 8N1, 흐름제어 없음**을 사용합니다.
3.3V USB-UART 어댑터를 사용하고 보드의 실제 핀 위치를 확인합니다.

| USB-UART | STM32 핀 | 역할 |
| --- | --- | --- |
| TXD | PC7 / USART6_RX | PC 측정·제어 요청 입력 |
| RXD | PC6 / USART6_TX | STATUS_REPORT 출력; 이진 트랙 결과는 미구현 |
| GND | GND | 공통 접지 |
| VCC / 5V / 3V3 | 연결하지 않음 | 보드와 어댑터는 각자 USB로 급전 |

TX→RX, RX←TX로 교차 연결합니다. 핀 변경 시 overlay도 함께 변경합니다.
ST-Link USB는 flash와 Zephyr 콘솔 로그에 사용하며 데이터용 USB-UART와 구분합니다.
프레임·TYPE·payload 규격은 [PC 도구 문서](../pc_sim/README.md)를 참고합니다.

## 1. 빌드·플래시

```bash
source ~/zephyrproject/.venv/bin/activate
cd ~/zephyrproject
west build -d <repo>/build -b stm32f746g_disco <repo>/fcc_app -- -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
west flash -d <repo>/build --runner openocd
```

빌드 성공, flash 성공, 보드 실행 성공을 각각 구분해서 기록합니다.

## 2. 포트와 콘솔

- ST-Link VCP: 부팅·측정·추적 로그. `=== FCC learning-rebuild boot ===` 확인.
- 외부 USB-UART: USART6 측정·제어 입력과 STATUS 응답 출력. 위 배선 참조.
- 하나의 데이터 포트를 송신기와 모니터가 동시에 열지 않도록 합니다.
- Linux 예: 콘솔 `/dev/ttyACM0`, 데이터 `/dev/ttyUSB0`.
  macOS는 각각 `cu.usbmodem...`, `cu.usbserial...` 등의 실제 장치명을 확인합니다.

일반 빌드는 INFO 이상만 출력합니다. 아래 측정·예측 로그를 확인하는 절차는
`debug.conf` 빌드가 필요합니다. STATUS의 일곱 통계는 일반 빌드에서도 wire로 조회합니다.
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
이진 트랙 응답과 폐루프 RMSE 검증은 아직 구현되지 않았습니다.

## 5. Overflow 보드 시험 (추후 수행)

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

최신 결과: [확장 STATUS 실보드 결과](hardware-test-2026-11-14-status-extended.md). 전체 이력은 [검증 기록](verification-history.md)을 따릅니다. 이전 측정·GUI 기록은 [2026-11-01 검증](hardware-test-2026-11-01.md)에 보존합니다.

VS Code의 로컬 기본 작업 `Zephyr: Build firmware`와 IntelliSense는 모두
`<repo>/build`를 사용합니다. 새 C 파일을 CMake에 등록한 뒤 빌드하면
`compile_commands.json`이 갱신됩니다. `.vscode/`는 Git 제외 대상입니다.


### IntelliSense 경로 오류 재발 확인

펌웨어와 호스트 테스트는 서로 다른 컴파일 환경을 사용합니다.
UART 테스트는 실제 `uart_rx.c`를 직접 포함하므로 테스트에 펌웨어용 기본
includePath가 적용되면 `cmsis_core.h` 등의 가짜 include 오류가 생길 수 있습니다.

로컬 `.vscode/c_cpp_properties.json`의 `compileCommands`에는 다음 다섯 경로를
순서대로 연결합니다. 펌웨어를 먼저 두어 공통 소스의 기본 분석을 유지합니다.

```json
"compileCommands": [
  "${workspaceFolder}/build/compile_commands.json",
  "${workspaceFolder}/build_uart_rx/compile_commands.json",
  "${workspaceFolder}/build_frame_handler/compile_commands.json",
  "${workspaceFolder}/build_uart_tx/compile_commands.json",
  "${workspaceFolder}/build_tracking/compile_commands.json"
]
```

- 펌웨어: `Zephyr: Build firmware` 작업으로 빌드와 데이터베이스를 갱신합니다.
- UART 호스트 테스트: `Host: Configure UART tests` 작업으로 별도 데이터베이스를
  생성합니다. 이 작업은 테스트 실행이 아니라 CMake 구성입니다.
- 공통 `compilerPath`, `intelliSenseMode`, `forcedInclude`로 ARM 환경을 강제하지
  않습니다. 컴파일러·매크로·생성 헤더는 각 데이터베이스 항목을 따릅니다.
- 새 펌웨어 C 파일은 `fcc_app/CMakeLists.txt`에 등록한 뒤 빌드합니다.
- 오류가 남으면 `C/C++: Log Diagnostics`에서 해당 파일의 compile commands
  항목을 확인합니다. 펌웨어는 ARM GCC와 Zephyr/CMSIS 경로, 호스트 테스트는
  호스트 컴파일러와 `tests/test_uart_rx/include` 모사 헤더를 사용해야 합니다.
- 파일 매핑이 이전 설정에 남으면 `C/C++: Reset IntelliSense Database`로
  재분석합니다. 오류 표시를 끄거나 시스템 헤더 전체를 재귀 추가하지 않습니다.

`build*`와 `.vscode`는 Git 제외 대상입니다. 새 체크아웃에서는
[VS Code 설정 예제](../tools/vscode/README.md)를 `.vscode/`에 복사한 뒤 firmware 빌드와
UART/STATUS/TX/Tracking host configure 작업을 실행합니다.
모듈 include는 `"protocol/frame_parser.h"`처럼 src 기준으로 작성하고,
CMake가 `fcc_app/src`를 include root로 전달합니다. 하위 폴더를 각각 includePath에 나열하지 않습니다.
실제 컴파일 실패는 IntelliSense 설정으로 해결하지 않고 별도로 확인합니다.


### STATUS 통계 확인 (기본 왕복 검증 완료, 재현 절차)

```sh
python -m pc_sim.status_client --port /dev/ttyUSB0 --timeout 2
```

현재 STATUS payload는 사용하지 않지만 프레임의 LEN은 1..32여야 합니다.
통계는 CLI/GUI의 이진 응답으로 확인합니다. 디버그 콘솔의 `STATUS report queued`는 큐 접수만 의미합니다.
CRC 오류·잘못된 길이·부분 프레임 중단을 주입한 뒤 재조회하고, INIT/RESET 후에도
누적값이 유지되는지 확인합니다. STATUS만 보내도 측정 무수신 timeout은 계속 동작해야 합니다.
펌웨어는 통계를 28바이트 STATUS_REPORT로 구성해 TX 큐에 복사 제출합니다.
CLI는 응답 TYPE·길이·CRC를 검사한 뒤 일곱 통계를 출력합니다.
GUI·다른 송신기를 닫아 포트를 단독 사용하고 PC6 TX → 어댑터 RXD 배선도 확인합니다.
정상 조회·반복 조회·오류 주입 후 통계 증가·추적 중 조회를 확인하고 로그와 펌웨어 버전을 남깁니다.
GUI 시험은 CLI 종료 후 STATUS 탭에서 별도로 수행합니다.
`2aa57d9`에서 이 기본 절차를 수행했습니다. 상세 범위와 로그는 아래 시험 기록을 참고합니다.


## 이전 STATUS 실보드 결과 (16바이트 규격)

`2aa57d9`에서 CLI·GUI STATUS 왕복, CRC/LEN/부분 프레임 timeout 통계 증가,
INIT/RESET 통계 보존, 추적 중 조회와 무수신 timeout을 확인했습니다.
[2026-11-12 시험 결과·원시 로그](hardware-test-2026-11-12-status.md)를 참고합니다.
이 기록은 이전 STATUS 실보드 미검증/연기 상태를 대체합니다. 강제 링 overflow·UART overrun·부하 시험은 남아 있습니다.


## 2026-11-14 Kconfig 실보드 회귀

`1e956c5`의 DRAIN·DISCARD 펌웨어를 각각 플래시하고 UART·STATUS·오류 처리 16개 조건씩 통과했습니다.
기본 DRAIN으로 복원하고 STATUS 응답까지 확인했습니다.
[결과와 원시 로그](hardware-test-2026-11-14-kconfig.md).
강제 링 overflow의 정책별 동작·UART 하드웨어 오류·부하 시험은 미검증이며 GUI는 이번에 재검증하지 않았습니다.


## fmodf·수치 보호 검증 (2026-11-14)

각도 정규화는 fmodf와 범위 보정으로 (-π, π]를 유지합니다.
비정상 s는 일반 if로 보정을 생략하며, 내부 유한성 assert는 디버그에서 활성화합니다.
[최신 보드 시험](hardware-test-2026-11-14-fmodf.md).
다음 작업은 deferred logging·로그 레벨·버퍼 포화 정책 정리이며 아직 미구현입니다.

## 게이트 설정 실보드 검증 (2026-11-14)

`610e01e`: 기본값 100 m/10°와 변경값 250 m/30° 각각 UART 16개·게이트 14개 통과.
REQ-TRK-002 / SPEC-TRACK의 거리 경계·각도 안팎·AND·wrap 및 설정에 따른 수용/거부 차이를 확인했다.
보드는 기본값/DRAIN으로 복원했다. 앞선 설정 변경의 실보드 미검증 기록은 이 결과로 보완한다.
[결과·원시 로그·검증 한계](hardware-test-2026-11-14-gating.md).
