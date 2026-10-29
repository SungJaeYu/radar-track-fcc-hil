# 현재 학습 펌웨어 보드 검증

현재 실행 경로는 USART6 수신 → 파싱 → 단일 트랙 추정 → ST-Link 로그입니다.
과거 LCD·CRC16 구현 문서와 달리 현재 소스에는 LCD 표시나 이진 트랙 응답 경로가 없습니다.

## 1. 빌드·플래시

```bash
source ~/zephyrproject/.venv/bin/activate
cd ~/zephyrproject
west build -p always -b stm32f746g_disco <repo>/fcc_app
west flash --runner openocd
```

빌드 성공, flash 성공, 보드 실행 성공을 각각 구분해서 기록합니다.

## 2. 포트와 콘솔

- ST-Link VCP: 부팅·측정·추적 로그. `=== FCC learning-rebuild boot ===` 확인.
- 외부 USB-UART: USART6 측정 입력. [배선](HIL-wiring.md) 참조.
- 하나의 데이터 포트를 송신기와 모니터가 동시에 열지 않도록 합니다.
- Linux 예: 콘솔 `/dev/ttyACM0`, 데이터 `/dev/ttyUSB0`.
  macOS는 각각 `cu.usbmodem...`, `cu.usbserial...` 등의 실제 장치명을 확인합니다.

## 3. 자동 INIT과 측정 송신

저장소 루트에서:

```bash
python pc_sim/radar_sender.py --port /dev/ttyUSB0 --count 20 --hz 10 --noise 0
```

- 콘솔에서 `TYPE_INIT`과 최초 `Filters initialized` 로그 확인.
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

## 5. 검증 기록

Python 테스트, 펌웨어 빌드, flash, 실제 보드 로그를 별도 결과로 남깁니다.
통합 도구의 테스트 통과만으로 새 보드 HIL까지 통과했다고 기록하지 않습니다.
