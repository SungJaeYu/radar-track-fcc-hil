# PC 레이더 시뮬레이터 · 보드 시험 도구

PC 측정 생성과 UART 시험 도구를 이 폴더 하나로 통합했습니다. 자동 테스트는 루트의 `tests/`에 있습니다.
모든 송신기는 현재 펌웨어의 CRC-16/IBM-3740 규격을 공유합니다. 이전 XOR 프레임은 호환되지 않습니다.

## 데스크톱 GUI

`python -m pip install -r pc_sim/requirements-gui.txt`로 선택 의존성을 설치하고,
저장소 루트에서 `python -m pc_sim.gui`로 실행합니다.
포트 선택·시작/중지·실시간 그래프·보드 콘솔 사용법은 [GUI 안내](GUI.md)를 참고하세요.
기존 CLI와 펌웨어는 그대로 사용할 수 있습니다.

## 파일 역할

- `small_byte_test.py`: 문자열을 담은 프레임 하나 송신. 기본 TYPE=1로 필터 초기화에 사용. TYPE=5 RESET도 지원.
- `radar_sender.py`: 거리·방위각 측정값을 생성하고 연속 송신. `--port` 생략 시 출력만 수행.
- `protocol.py`: 모든 송신기가 공유하는 프레임 생성·CRC-16·라디안 변환.
- `targets.py`: 등속 표적, 잡음, 미탐지, 클러터 모델.
- `run_sim.py`: 표적 시나리오를 현재 규격으로 송신하거나 프레임만 출력.

프레임: `7E | payload 길이 | TYPE | payload | CRC_H | CRC_L`.
레이더 TYPE은 `2`, payload는 big-endian `uint32 timestamp_ms + uint16 range_m + int32 azimuth_sc`의 10바이트(`struct.pack(">IHi", ...)`)입니다.
`--azimuth`는 degree가 아닌 int32 semicircle 값(-2147483648..2147483647)을 직접 받습니다.
변환식은 `azimuth_rad = azimuth_sc × π / 2³¹`이며, `1073741824`는 90°, `-1073741824`는 -90°입니다.
기본값은 기존 약 30° 방향을 유지하는 `178956971`입니다. FCC는 수신 후 라디안으로 변환합니다.

## 실행

저장소 루트에서 실행합니다. 기존 `tools/hil/` 명령은 아래 `pc_sim/` 경로로 바뀌었습니다.

```bash
source ~/zephyrproject/.venv/bin/activate

# 수동 초기화만 할 때 (아래 연속 송신기는 시작 시 자동 INIT)
python pc_sim/small_byte_test.py init --type 1 --port /dev/cu.usbserial-A5069RR4

# 잡음 없이 1000m부터 초당 20m씩 접근, 10Hz로 20개 송신
python pc_sim/radar_sender.py --port /dev/cu.usbserial-A5069RR4 --hz 10 --count 20 --noise 0

# 보드 없이 프레임 출력 확인
python pc_sim/radar_sender.py --count 3 --noise 0
```

포트 이름은 연결 환경에 맞게 바꾸세요. 송신 스크립트는 실행마다 timestamp를 0부터 시작하므로,
매 실행 시 INIT을 자동 전송해 이전 트랙을 초기화합니다.
결과는 ST-Link 콘솔(`radar-console`)에서 확인합니다. USB-UART 포트는 다른 모니터에서 열지 마세요.

## 자동 검증

```bash
python -m pytest
```

`tests/`만 실행하며 보드 연결이나 flash를 수행하지 않습니다.
펌웨어의 [UART 복구](../fcc_app/tests/test_uart_rx/README.md)와
[Tracking C 테스트](../fcc_app/tests/test_kalman/README.md)는 별도 CMake/CTest 명령으로 실행합니다.

## 시나리오 실행

```bash
# 보드 없이 단일 표적 측정 출력 (실시간 대기 없음)
python -m pc_sim.run_sim --duration 1 --noise 0 --azimuth-noise 0

# 시작 시 INIT을 자동 전송한 뒤 시나리오 실행
python -m pc_sim.run_sim --port /dev/cu.usbserial-A5069RR4 --duration 10

# 다중표적·클러터는 PC 출력 전용
python -m pc_sim.run_sim --scenario two_crossing --clutter 0.5 --duration 1

# 미탐지 모델: 각 표적을 스캔마다 80% 확률로 탐지
python -m pc_sim.run_sim --pd 0.8 --duration 1
```

- `--port` 생략은 출력 전용이며, 보드 동작을 모사하는 mock 추적기가 아닙니다.
- 이전 `--mock`은 포트 생략으로, `--serial`은 `--port`로 대체했습니다.
- `radar_sender.py`는 거리 변화율을 직접 지정하는 간단한 송신기입니다.
  `run_sim.py`는 직교좌표 표적 운동을 극좌표 측정으로 변환하는 시나리오 실행기입니다.
  둘 다 같은 `protocol.py`를 사용하며 서로 다른 통신 규격을 유지하지 않습니다.
- 현재 보드에는 다중 측정 데이터 연관이 없으므로 UART 모드는 단일 표적·클러터 0만 허용합니다.
  여러 표적의 동일 timestamp 측정을 보내면 첫 측정 이후 중복 timestamp로 거부될 수 있습니다.
- 센서 모델의 거리(m)는 정수로 반올림하고, 방위각(rad)은 signed int32 semicircle로 변환합니다.
  반올림 후 거리 1..65535m 밖의 측정은 이유를 출력하고 건너뜁니다.
- 모델의 앙각·도플러는 PC 내부 물리량이며 현재 10바이트 payload에는 없습니다.
- 기본 시나리오는 `single_approach`, 탐지확률 1, 클러터 0입니다.
  `--noise`는 거리 표준편차(m), `--azimuth-noise`는 방위각 표준편차(rad)입니다.
- 보드의 이진 트랙 응답·RMSE 계산은 아직 지원하지 않습니다. 결과는 ST-Link 콘솔로 확인합니다.
- 두 연속 송신기 모두 실행마다 `7e 01 01 00 c8 9d` INIT을 한 번 보낸 뒤 timestamp를 0부터 시작합니다.
  출력 전용 모드에도 INIT이 먼저 표시됩니다. INIT은 측정 개수(`--count`)에 포함되지 않습니다.
  `flush()`는 송신 완료 대기이며 보드의 처리 완료 응답은 아닙니다.

## 수신 버퍼와 부하 시험

현재 펌웨어 RX 링버퍼는 256바이트 중 255바이트를 저장합니다.
overflow 시 기본 drain 정책은 저장된 데이터로 완성 가능한 프레임을 처리하고,
복구 중 새 입력과 남은 부분 프레임은 버립니다.

`--hz`는 측정 생성·송신 주기를 정하는 옵션이며 보드의 처리 완료를 기다리는 기능은 아닙니다.
송신 속도를 높였다는 사실이나 PC 출력만으로 overflow 발생·복구를 판정하지 않습니다.
보드의 복구 로그와 이후 정상 측정 처리를 확인하는 [보드 시험 절차](../docs/board-bringup.md)를 참고하세요.
현재 펌웨어에는 정확한 손실량을 보고하는 카운터나 이진 응답이 없습니다.

## 통합과 이력

이전 XOR 규격은 현재 CRC-16 펌웨어와 호환되지 않습니다.
이전 구현은 Git 이력에 남아 있으며, 구버전과 신버전 송신기를 섞어 사용하지 않습니다.
현재와 맞지 않는 과거 설계·계획 문서는 제거했습니다. 현재 실행 방법은 이 문서를 따릅니다.
