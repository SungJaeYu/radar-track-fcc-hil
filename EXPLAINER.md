# 현재 코드 흐름 설명

이 문서는 현재 단일 트랙 학습 구현을 설명합니다. 실행 명령은 [pc_sim 안내](pc_sim/README.md),
배선은 [HIL-wiring](docs/HIL-wiring.md)를 참고하세요.

## 1. PC 측정 생성

`pc_sim/radar_sender.py`는 거리 변화율과 잡음으로 간단한 측정을 생성합니다.
`pc_sim/run_sim.py`는 `targets.py`의 직교좌표 표적을 움직이고 센서 모델로 측정을 생성합니다.
두 실행기는 `protocol.py`의 동일한 프레임 생성 함수를 사용합니다.

시나리오의 물리량은 거리 m, 각도 rad입니다. 송신 직전에 거리를 정수 m로 반올림하고
각도를 signed int32 semicircle로 변환합니다. 앙각·도플러는 모델 내부에만 남습니다.
다중표적·클러터 생성은 PC 출력에서 가능하지만 현재 보드 전송에는 단일 표적만 허용합니다.
`--port` 생략은 프레임 출력이며 보드 추적기의 모사가 아닙니다.

## 2. 통신 규격

```text
7E | LEN | TYPE | PAYLOAD | XOR
```

LEN은 payload 길이 1..32바이트이고 XOR 범위는 TYPE과 payload입니다.
TYPE_RADAR_MEAS=2의 payload는 `>IHi` 10바이트입니다.

| 필드 | 크기 | 의미 |
|---|---|---|
| timestamp_ms | 4바이트 | 송신 측 uint32 시간(ms) |
| range_m | 2바이트 | uint16 거리(m), 유효 범위 1..65535 |
| azimuth_sc | 4바이트 | signed int32 semicircle |

보드 변환식은 `azimuth_rad = azimuth_sc × π / 2³¹`입니다.
TYPE_INIT=1은 이전 트랙을 초기화합니다. payload 길이 0은 파서가 거부하므로
수동 INIT에도 문자열 등 최소 1바이트를 넣습니다.
두 연속 송신기는 매 실행 시 `7e 01 01 00 01` INIT 프레임을 한 번 보낸 뒤 측정을 시작합니다.
자동 INIT의 payload는 더미 바이트 `0x00`입니다. 출력 전용 모드에서도 INIT이 먼저 표시됩니다.

## 3. UART 수신

`uart_callback()`은 인터럽트에서 바이트를 링버퍼에 넣고 `rx_sem`으로 수신 스레드를 깨웁니다.
실제 데이터는 링버퍼에 있고 세마포어는 알림입니다.
`rx_ready_sem`은 링버퍼·파서·handler 초기화가 끝나기 전 처리를 막습니다.

`uart_rx_thread()`는 RX 알림 또는 50ms 대기 만료 후 실행됩니다.
링버퍼가 비어 있어도 timeout을 검사하고, 바이트가 계속 들어올 때도 내부 루프에서 검사합니다.
`parser_feed()`가 정상 체크섬의 프레임을 완성하면 `handle_frame()`이 처리합니다.

### 버퍼와 overflow

소프트웨어 링버퍼는 256바이트이며 한 칸을 비워 실제로 255바이트를 저장합니다.
head/tail은 `uint8_t`로 0..255를 표현합니다. 버퍼를 256보다 크게 늘리려면 인덱스 타입도 바꿔야 합니다.

ISR에서 `ring_push()`가 실패하면 해당 바이트를 버리고 `rx_overflow`를 설정한 뒤
`rx_sem`으로 스레드에 알립니다. 플래그가 설정된 동안 ISR은 하드웨어 FIFO를 읽되 새 바이트는 저장하지 않습니다.
일반 수신 루프는 IRQ 잠금 안에서 overflow 확인·한 바이트 pop·파싱을 수행하고,
완성된 프레임의 handler는 잠금을 해제한 뒤 호출합니다.

현재 헤더의 `RX_OVERFLOW_POLICY`는 `RX_OVERFLOW_DRAIN_BUFFER`입니다.
복구는 IRQ를 허용한 상태에서 기존 파서와 버퍼로 완성할 수 있는 프레임을 처리합니다.
마지막에 짧게 IRQ를 잠그고 링버퍼·부분 프레임을 초기화한 뒤 overflow를 해제합니다.
`RX_OVERFLOW_DISCARD_ALL`을 선택하면 drain 없이 바로 초기화합니다. Kconfig 선택은 아직 없습니다.
복구 중 새 입력은 손실되며, 초기화는 하드웨어 FIFO를 비우거나 다음 프레임 경계 정렬을 보장하지 않습니다.

## 4. 측정과 추적

`decode_radar_measurement()`와 `validate_radar_measurement()`를 통과한 측정은
보드 uptime 기반 마지막 수신 시각을 갱신합니다. 송신 timestamp와 보드 uptime은 서로 다른 시계입니다.

- EMPTY이면 `track_start()`로 필터를 초기화하고 TENTATIVE가 됩니다.
- 기존 트랙이면 송신 timestamp 차이로 예측하고 게이트를 검사합니다.
- 게이트 통과는 `track_hit()`, 탈락은 `track_miss()`로 처리합니다.
- 3회 hit로 CONFIRMED, 확정 트랙의 miss는 COASTING, 연속 5회 miss로 LOST가 됩니다.
- TENTATIVE에서 miss면 EMPTY로 초기화합니다.
- 중복·역행 timestamp는 거부합니다. 순방향 간격 1000ms 초과는 트랙을 초기화합니다.

## 5. 무수신 timeout

정상 레이더 측정을 처리한 이후 1000ms 이상 새 정상 측정이 없으면 handler를 초기화합니다.
첫 정상 측정 전에는 감시하지 않으며, timeout 후에는 다음 정상 측정까지 감시를 해제합니다.
STATUS나 잘못된 패킷은 수신 시각을 갱신하지 않습니다.
게이트 또는 timestamp 검사에서 거부된 측정도 decode·validate를 통과했다면 수신 시각은 갱신합니다.
이는 필터 보정 성공 여부가 아니라 정상 측정 payload의 수신 중단을 감시하는 정책입니다.

## 6. 검증의 범위

Python 테스트는 프레임 바이트·단위 변환·송신 순서·시나리오 CLI를 검증합니다.
`fcc_app/tests/test_kalman`은 C 필터·게이팅·추적 정책 등의 별도 테스트입니다.
[UART host 테스트](fcc_app/tests/test_uart_rx/README.md)는 실제 ISR·복구·링버퍼·파서의 8개 사례를 검증합니다.
UART·IRQ·atomic·시각·handler는 모사하며 무한 RX 스레드를 실제로 스케줄링하지 않습니다.
이 테스트들은 새 펌웨어 빌드나 실제 UART HIL 성공을 대신하지 않습니다.
현재 보드는 이진 트랙 응답을 송신하지 않으므로 PC의 폐루프 RMSE 계산은 아직 없습니다.
구버전 CRC16·LCD·다중 트랙 문서는 제거했으며 과거 기록은 Git 이력에서 확인할 수 있습니다.
