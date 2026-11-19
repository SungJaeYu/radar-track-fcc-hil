# radar-track-fcc-hil

**V1 완료 · 2026-11-19**

PC가 합성 레이더 측정을 보내면 STM32가 표적을 추적하고 결과를 반환하는 HIL 프로젝트입니다. STM32F746G-DISCO와 Zephyr 4.2에서 단일표적 CV 칼만 필터를 실행하며, PC에서 정답·측정·보드 추정값을 비교합니다.

- **펌웨어:** UART ISR·링버퍼·RX/handler/TX 스레드, CRC 검증, 게이팅·트랙 상태 관리, STATUS/TRACK 보고.
- **PC:** 합성 측정 송신, GUI, CSV 기록, 정상·잡음·연속 누락 시나리오의 RMSE 평가.
- **검증:** 실보드 결과 왕복, 오류·큐 포화 후 복구, 50Hz 목표 부하의 30분 송수신·지연·스택 관측.

> 문제 정의·구조·검증 기준과 STM32/Zephyr 펌웨어는 직접 설계·작성했습니다. PC 시뮬레이터와 GUI 구현에는 AI 코딩 에이전트를 활용했습니다. 데이터는 합성 측정이며 실제 군 운용 데이터나 내부 시스템 정보를 사용하지 않습니다.

## 송수신 성능

2026-11-19 종료한 **30분 실보드 시험** 기준입니다. `perf.conf`·DRAIN, USART6 **115200 baud / 8N1**, 정지 단일표적·잡음 0, MEAS 목표 50Hz·STATUS 1Hz 조건입니다.

| 항목 | 관측 결과 |
| --- | ---: |
| 측정 송신 / 대응 결과 수신 | **89,990 / 89,990건** |
| 실제 평균 측정 송신률 | 49.994Hz |
| PC 왕복 지연 p50 / p95 | 6.386 / 6.633ms |
| PC 왕복 지연 p99 / 최대 | **8.049 / 34.894ms** |
| 보드 RX 완료 → TX 큐 제출 최대 | **1ms** (20ms 초과 0건) |
| 보드 TX 큐 접수 / polling 완료 / PC 수신 | 91,793 / 91,793 / 91,793프레임 |
| STATUS 응답 | 1,801건, 여덟 오류·폐기 카운터 모두 0 |

PC 왕복은 write 직전부터 대응 TRACK이 포함된 read 직후까지이며 USB·UART·보드·호스트 수신 대기를 포함합니다. STM32만의 처리시간은 아니며, 보드 내부 지연은 별도로 관측합니다. 보드 수치는 ms 단위 관측값으로 UART 전송 완료까지의 시간이나 WCET가 아닙니다.

**현재 왕복 기준(p99 ≤20ms, 최대 ≤100ms)에 위 관측값이 들어옵니다.** PC 송신 간격은 호스트 환경의 영향을 받아 STM32 성능을 독립적으로 판단하기 어려우므로 합격 조건에서 제외하고 실제 부하를 설명하는 참고 통계로 남겼습니다. 당시 송신 간격 기준에 따른 원시 실패 기록은 보존합니다. 이번 정리는 새 실보드 시험 결과가 아닙니다.

[시험 조건·상세 통계·원시 로그](docs/hardware-test-2026-11-19-performance.md) · [현재 측정·판정 기준](docs/performance-monitoring.md) · [추적 정확도 결과](docs/hardware-test-2026-11-18-evaluation.md)

## 실행

보드 데이터 연결은 **USB-UART TXD → PC7(RX), RXD ← PC6(TX), GND 공통**입니다. 3.3V 어댑터를 사용하며, 각자 USB로 급전하므로 전원선은 연결하지 않습니다. ST-Link는 플래시·진단 콘솔에 별도로 사용합니다.

Zephyr 환경을 준비한 뒤 펌웨어를 빌드·플래시합니다. 아래 저장소 경로는 실제 경로로 바꿉니다.

```bash
source ~/zephyrproject/.venv/bin/activate
cd ~/zephyrproject
west build -d /path/to/radar-track-fcc-hil/build -b stm32f746g_disco /path/to/radar-track-fcc-hil/fcc_app
west flash -d /path/to/radar-track-fcc-hil/build --runner openocd
```

같은 Python 환경에서 저장소 루트로 이동해 GUI를 실행합니다. 데이터 포트와 ST-Link 콘솔 포트를 구분해 선택합니다.

```bash
python -m pip install -r pc_sim/requirements-gui.txt
python -m pc_sim.gui
```

[배선·빌드·보드 확인](docs/board-bringup.md) · [GUI 안내](pc_sim/GUI.md) · [CLI·CSV 평가](pc_sim/README.md)

## 문서 안내

| 확인할 내용 | 문서 |
| --- | --- |
| V1 완료 범위·후속 과제 | [로드맵](docs/roadmap.md) |
| ISR·스레드·큐·프로토콜·소스 구조 | [구현 흐름](EXPLAINER.md) |
| PC와 보드의 책임·ID 정책 | [시스템 역할](docs/system-roles.md) |
| 요구사항과 구현·시험 연결 | [요구사항 추적표](docs/requirements-traceability.md) |
| 테스트 실행과 날짜별 결과 | [테스트 목록](docs/test-cases.md) · [검증 이력](docs/verification-history.md) |
| 코드·로그·개발 환경 규칙 | [코딩 스탠다드](docs/coding-standard.md) · [로깅](docs/logging.md) · [VS Code](tools/vscode/README.md) |

<a id="portfolio-v1"></a>
<a id="v1-acceptance"></a>

V1은 **단일표적 결과 왕복·정량 평가·주요 오류 복구·시간/자원 관측**까지 완료했습니다. 다중표적·CT/IMM·BIT·Lock On은 후속 선택 과제입니다. 실제 RF·ADC/IQ·FFT와 항공 인증은 범위 밖이며, 이번 시험은 선언한 조건에서의 관측 결과입니다. [완료 범위와 한계](docs/roadmap.md#v1-acceptance).
