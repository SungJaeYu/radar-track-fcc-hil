# 현재 펌웨어의 HIL 배선·통신

기준은 `fcc_app/boards/stm32f746g_disco.overlay`입니다.
현재 설정은 **USART6 TX=PC6, RX=PC7, 115200 baud**입니다.
예전 문서의 PG14/PG9 배선 안내는 현재 overlay와 맞지 않아 제거했습니다.

## 두 연결의 역할

- ST-Link USB: flash와 Zephyr 콘솔 로그 확인.
- 외부 USB-UART: PC에서 생성한 측정을 USART6로 입력.
- 현재 펌웨어는 이진 트랙 결과를 UART로 반환하지 않습니다. 처리 결과는 ST-Link 로그로 확인합니다.

```text
PC의 pc_sim → 외부 USB-UART TX → STM32 PC7 (USART6_RX)
PC의 콘솔   ← ST-Link VCP     ← STM32 printk 로그
```

## 배선

3.3V UART 어댑터를 사용하고, 현재 보드의 PC6/PC7 핀 위치를 확인해 연결합니다.

| USB-UART | STM32 핀 | 역할 |
|---|---|---|
| TXD | PC7 / USART6_RX | 측정 데이터 입력 |
| RXD | PC6 / USART6_TX | 현재 이진 트랙 송신은 미구현 |
| GND | GND | 공통 접지 |
| VCC / 5V / 3V3 | 연결하지 않음 | 보드와 어댑터는 각자 USB로 급전 |

TX→RX, RX←TX로 교차 연결합니다. 핀 배치를 바꿀 경우 문서뿐 아니라 overlay도 함께 변경해야 합니다.
물리 설정은 115200, 8N1, 흐름제어 없음입니다.

Linux 포트 예시는 ST-Link `/dev/ttyACM0`, 데이터 `/dev/ttyUSB0`입니다.
macOS에서는 `cu.usbmodem...`, `cu.usbserial...` 등 실제 장치명을 사용합니다.
데이터 포트를 송신기와 별도 모니터에서 동시에 열지 마세요.

## 프레임

```text
7E | LEN(1) | TYPE(1) | PAYLOAD(LEN) | XOR(1)
```

- LEN: payload 1..32바이트.
- 체크섬: TYPE과 PAYLOAD를 XOR.
- 측정 TYPE=2, payload=`>IHi` 10바이트: uint32 ms, uint16 m, int32 semicircle.
- INIT TYPE=1은 트랙과 timeout 감시를 초기화합니다.

## 수신 버퍼

펌웨어 소프트웨어 링버퍼는 256바이트(실제 저장 용량 255바이트)입니다.
이는 UART 하드웨어 FIFO 크기와 다르며 버퍼 변경으로 baud나 핀 배선이 바뀌지 않습니다.
현재 overflow 복구는 drain 정책이고 복구 중 새 입력은 버립니다.
부하·복구 검증은 [보드 시험 절차](board-bringup.md)를 따릅니다.

## 연결·검증

1. 빌드·flash 후 ST-Link 콘솔에서 부팅 로그를 확인합니다.
2. 어댑터 배선과 데이터 포트를 확인합니다.
3. 저장소 루트에서 실행합니다. 송신기가 INIT을 먼저 한 번 보내고 측정을 시작합니다.

```bash
python pc_sim/radar_sender.py --port /dev/ttyUSB0 --count 20 --noise 0
```

4. 콘솔의 `TYPE_INIT`, `Filters initialized`, 예측·측정 처리 로그를 확인합니다.
5. 시나리오 시험도 자동 INIT을 포함하므로 아래 명령으로 실행합니다.

```bash
python -m pc_sim.run_sim --port /dev/ttyUSB0 --duration 10
```

PC 송신 출력만으로 보드 수신 성공을 판정하지 않습니다.
송신 중단 후 timeout은 첫 정상 측정을 받은 경우에만 감시하며, 현재 기준은 1초입니다.
상세 절차는 [보드 검증](board-bringup.md), 옵션은 [PC 도구](../pc_sim/README.md)를 참고하세요.
