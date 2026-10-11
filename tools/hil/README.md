# 현재 STM32 학습 펌웨어용 보드 시험 도구

이 폴더는 직접 실행하는 UART 시험 도구입니다. 자동 테스트는 루트의 `tests/`에 있습니다.
`pc_sim/`의 기존 CRC16 시뮬레이터와는 통신 규격이 다릅니다.

## 파일 역할

- `small_byte_test.py`: 문자열을 담은 프레임 하나 송신. 기본 TYPE=1로 필터 초기화에 사용.
- `radar_sender.py`: 거리·방위각 측정값을 생성하고 연속 송신. `--port` 생략 시 출력만 수행.
- `protocol.py`: 두 도구가 공유하는 프레임 생성과 XOR 체크섬 계산.

프레임: `7E | payload 길이 | TYPE | payload | XOR(TYPE, payload)`.
레이더 TYPE은 `2`, payload는 big-endian `uint32 timestamp_ms + uint16 range_m + int32 azimuth_sc`의 10바이트(`struct.pack(">IHi", ...)`)입니다.
`--azimuth`는 degree가 아닌 int32 semicircle 값(-2147483648..2147483647)을 직접 받습니다.
변환식은 `azimuth_rad = azimuth_sc × π / 2³¹`이며, `1073741824`는 90°, `-1073741824`는 -90°입니다.
기본값은 기존 약 30° 방향을 유지하는 `178956971`입니다. FCC는 수신 후 라디안으로 변환합니다.

## 실행

저장소 루트에서 실행합니다. 기존 `pc_sim/` 경로의 두 송신 스크립트는 이 폴더로 이동했습니다.

```bash
source ~/zephyrproject/.venv/bin/activate

# 보드의 이전 필터 상태 초기화
python tools/hil/small_byte_test.py init --type 1 --port /dev/cu.usbserial-A5069RR4

# 잡음 없이 1000m부터 초당 20m씩 접근, 10Hz로 20개 송신
python tools/hil/radar_sender.py --port /dev/cu.usbserial-A5069RR4 --hz 10 --count 20 --noise 0

# 보드 없이 프레임 출력 확인
python tools/hil/radar_sender.py --count 3 --noise 0
```

포트 이름은 연결 환경에 맞게 바꾸세요. 송신 스크립트는 실행마다 timestamp를 0부터 시작하므로,
반복 시험 시 먼저 INIT을 보내 필터를 초기화하세요.
결과는 ST-Link 콘솔(`radar-console`)에서 확인합니다. USB-UART 포트는 다른 모니터에서 열지 마세요.

## 자동 검증

```bash
python -m pytest
```

`tests/`만 실행하며 보드 연결이나 flash를 수행하지 않습니다.
