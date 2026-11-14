# 현재 코드 흐름 설명

이 문서는 현재 단일 트랙 학습 구현을 설명합니다. 실행 명령은 [pc_sim 안내](pc_sim/README.md),
배선은 [배선·보드 검증](docs/board-bringup.md)를 참고하세요.

## 1. PC 측정 생성

`pc_sim/radar_sender.py`는 거리 변화율과 잡음으로 간단한 측정을 생성합니다.
`pc_sim/run_sim.py`는 `targets.py`의 직교좌표 표적을 움직이고 센서 모델로 측정을 생성합니다.
두 실행기는 `protocol.py`의 동일한 프레임 생성 함수를 사용합니다.

시나리오의 물리량은 거리 m, 각도 rad입니다. 송신 직전에 거리를 정수 m로 반올림하고
각도를 signed int32 semicircle로 변환합니다. 앙각·도플러는 모델 내부에만 남습니다.
다중표적·클러터 생성은 PC 출력에서 가능하지만 현재 보드 전송에는 단일 표적만 허용합니다.
`--port` 생략은 프레임 출력이며 보드 추적기의 모사가 아닙니다.

### 데스크톱 GUI

`python -m pc_sim.gui`는 기존 CLI와 함께 사용하는 PySide6/pyqtgraph 앱입니다.
`gui_model.py`는 단일표적 설정·측정 생성, `gui_worker.py`는 작업 스레드에서 포트 소유·송신·STATUS 수신·콘솔 읽기를 담당합니다.
`gui.py`는 signal로 전달된 데이터를 화면에 표시합니다. 미리보기는 시리얼 포트를 열지 않습니다.
송신을 중지해도 콘솔 연결은 유지하며, 재시작할 때 INIT을 먼저 보냅니다.
그래프는 합성 실제값·PC 측정값이며 보드 추정값은 아직 그래프로 반환하지 않습니다.
설치·실행·검증 한계는 [GUI 안내](pc_sim/GUI.md)에 정리했습니다.

## 2. 통신 규격

```text
7E | LEN | TYPE | PAYLOAD | CRC_H | CRC_L
```

LEN은 payload 길이 1..32바이트이고 CRC 범위는 LEN·TYPE·payload입니다.
FRAME_TYPE_MEAS_REPORT=2의 payload는 `>IHi` 10바이트입니다.

| 필드 | 크기 | 의미 |
|---|---|---|
| timestamp_ms | 4바이트 | 송신 측 uint32 시간(ms) |
| range_m | 2바이트 | uint16 거리(m), 유효 범위 1..65535 |
| azimuth_sc | 4바이트 | signed int32 semicircle |

보드 변환식은 `azimuth_rad = azimuth_sc × π / 2³¹`입니다.
FRAME_TYPE_INIT=1은 이전 트랙을 초기화합니다. payload 길이 0은 파서가 거부하므로
수동 INIT에도 문자열 등 최소 1바이트를 넣습니다.
두 연속 송신기는 매 실행 시 `7e 01 01 00 c8 9d` INIT 프레임을 한 번 보낸 뒤 측정을 시작합니다.
자동 INIT의 payload는 더미 바이트 `0x00`입니다. 출력 전용 모드에서도 INIT이 먼저 표시됩니다.

## 3. UART 수신

`uart_callback()`은 인터럽트에서 바이트를 링버퍼에 넣고 `rx_sem`으로 수신 스레드를 깨웁니다.
실제 데이터는 링버퍼에 있고 세마포어는 알림입니다.
`rx_ready_sem`은 링버퍼·파서 초기화가 끝나기 전 처리를 막습니다.

`uart_rx_thread()`는 RX 알림 또는 50ms 대기 만료 후 실행됩니다.
IRQ 잠금 안에서는 overflow 확인과 한 바이트 pop만 수행합니다.
잠금을 해제한 뒤 `rx_frame_processor_feed()`로 파싱하고, 정상 프레임을 handler 큐에 복사합니다.
안쪽 배출 루프가 끝나면 부분 프레임 timeout을 검사합니다.
별도 handler 스레드가 큐를 받아 `handle_frame()`을 호출하고 측정 timeout을 검사합니다.

### 버퍼와 overflow

소프트웨어 링버퍼는 256바이트이며 한 칸을 비워 실제로 255바이트를 저장합니다.
head/tail은 `uint8_t`로 0..255를 표현합니다. 버퍼를 256보다 크게 늘리려면 인덱스 타입도 바꿔야 합니다.

ISR에서 `ring_push()`가 실패하면 해당 바이트를 버리고 `rx_overflow`를 설정한 뒤
`rx_sem`으로 스레드에 알립니다. 플래그가 설정된 동안 ISR은 하드웨어 FIFO를 읽되 새 바이트는 저장하지 않습니다.
파싱과 큐 제출은 IRQ 잠금 밖에서 실행되며, 실제 측정 처리는 handler 스레드가 맡습니다.

현재 헤더의 `RX_OVERFLOW_POLICY`는 `RX_OVERFLOW_POLICY_DRAIN_BUFFER`입니다.
복구는 IRQ를 허용한 상태에서 기존 파서와 버퍼로 완성할 수 있는 프레임을 처리합니다.
마지막에 짧게 IRQ를 잠그고 링버퍼·부분 프레임을 초기화한 뒤 overflow를 해제합니다.
`RX_OVERFLOW_POLICY_DISCARD_ALL`을 선택하면 drain 없이 바로 초기화합니다. `fcc_app/Kconfig` choice와 `prj.conf`로 선택하며 기본값은 DRAIN입니다.
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

Python 테스트는 프레임 바이트·단위 변환·송신 순서·시나리오 CLI와 GUI 모델을 검증합니다.
GUI 의존성을 설치하면 offscreen Qt·모사 serial로 시작·중지·재시작·오류·포트 해제도 검사합니다.
`fcc_app/tests/test_kalman`은 C 필터·게이팅·추적 정책 등의 별도 테스트입니다.
[UART host 테스트](fcc_app/tests/test_uart_rx/README.md)는 실제 ISR·복구·링버퍼·파서·timeout·통계를 drain 21·discard 18, 총 39회 실행으로 검증합니다.
UART·IRQ·atomic·시각·handler는 모사하며 무한 RX 스레드를 실제로 스케줄링하지 않습니다.
이 테스트들은 새 펌웨어 빌드나 실제 UART HIL 성공을 대신하지 않습니다.
현재 보드는 이진 트랙 응답을 송신하지 않으므로 PC의 폐루프 RMSE 계산은 아직 없습니다.
구버전 LCD·다중 트랙 문서는 제거했으며 과거 기록은 Git 이력에서 확인할 수 있습니다.


## 7. STATUS 응답 송신

`rx_frame_processor.c`가 CRC·길이 오류·부분 프레임 timeout·큐 드롭의 atomic
카운터를 소유합니다. STATUS는 처리 스레드에서 지역 `stats`로 값을 복사해
ST-Link 콘솔에 출력합니다. 개별 값은 안전하게 읽지만 전체 항목을 같은 순간에
고정하지는 않습니다. 조회·parser reset·INIT/RESET은 누적 카운터를 지우지 않습니다.
이 통계는 하드웨어 BIT 결과가 아니며 링버퍼 손실·UART 하드웨어 오류는 아직 집계하지 않습니다.

`uart_tx.c/.h`는 초기화·TX 복사 큐·스레드와 polling 송신을 구현했습니다.
`protocol_frame_encode()`가 STX·LEN·TYPE·payload·CRC 바이트열을 구성합니다.
STATUS 처리부는 네 통계값을 big-endian uint32 순서로 인코딩한 16바이트
STATUS_REPORT를 TX 큐에 복사 제출합니다. 순서는 CRC 오류·길이 오류·부분 프레임
timeout·큐 드롭입니다. 인코딩·큐 제출 실패는 오류 코드와 함께 로그로 남깁니다.
`frame_parser.py`가 PC 수신 프레임을 조립·검증하고 `decode_status_payload()`가 통계를 해석합니다.
`status_client.py`는 CLI 왕복을, GUI의 STATUS 탭은 기존 worker를 통한 수동 조회를 제공합니다.
둘 다 host 모사 시험을 통과했으며 [2026-11-12 실보드 시험](docs/hardware-test-2026-11-12-status.md)에서 CLI·GUI STATUS 왕복도 확인했습니다.
TX host 테스트 4개와 CRC/인코더 상호운용 28개를 통과했으며 실보드 STATUS 송신도 확인했습니다. TX 포화·최대 부하는 별도 검증 대상입니다.
handler host 테스트 17개는 STATUS 응답·실패 복귀, INIT/RESET과 정상·잘못된 측정의 timeout 영향을 검증합니다.
통계 snapshot은 모사하며 실제 RX atomic 카운터와 handler 명령의 통합 시험은 남아 있습니다.


## 2026-11-14 Kconfig 실보드 회귀

`1e956c5`의 DRAIN·DISCARD 펌웨어를 각각 플래시하고 UART·STATUS·오류 처리 16개 조건씩 통과했습니다.
기본 DRAIN으로 복원하고 STATUS 응답까지 확인했습니다.
[결과와 원시 로그](docs/hardware-test-2026-11-14-kconfig.md).
강제 링 overflow의 정책별 동작·UART 하드웨어 오류·부하 시험은 미검증이며 GUI는 이번에 재검증하지 않았습니다.


## fmodf·수치 보호 검증 (2026-11-14)

각도 정규화는 fmodf와 범위 보정으로 (-π, π]를 유지합니다.
비정상 s는 일반 if로 보정을 생략하며, 내부 유한성 assert는 디버그에서 활성화합니다.
[최신 보드 시험](docs/hardware-test-2026-11-14-fmodf.md).
다음 작업은 deferred logging·로그 레벨·버퍼 포화 정책 정리이며 아직 미구현입니다.
