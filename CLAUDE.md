# radar-track-fcc-hil

## 프로젝트 정체성
다중표적 레이더 트래킹을 STM32에서 실시간 처리하는 Hardware-in-the-Loop(HIL) 시스템.
PC가 레이더 환경/측정을 시뮬레이션하고, STM32(FCC 역할)가 트래킹한 결과를
다시 PC로 돌려보내 정량 검증하는 폐루프 구조.

핵심 포지셔닝: "검증을 아는 개발자". 따라서 모든 기능은
정량 검증 가능한 형태로 만든다 (RMSE, 트랙 연속성 등).

## 개발 환경 (이미 검증 완료)
- 보드: STM32F746G-DISCO (Cortex-M7), 보드 ID: `stm32f746g_disco`
- RTOS: Zephyr v4.2, SDK: ~/zephyr-sdk-1.0.1
- west workspace: ~/zephyrproject
- 빌드: `west build -p always -b stm32f746g_disco <app_path>`
- 굽기: `west flash --runner openocd`  (← 반드시 openocd 러너 사용)
- 시리얼 콘솔: tio /dev/ttyACM0 (115200 8N1)
- 작업 전 항상: `source ~/zephyrproject/.venv/bin/activate`

## 아키텍처 (3계층)
1. PC측 (Python, 이미 구현/검증됨): 표적 시나리오 + 레이더 측정 모델
   (가우시안 노이즈, Pd, 클러터/오탐) + UART 송수신 + ground truth 비교
2. 통신 계층: UART 프레임 `STX | len | type | payload | CRC16`,
   메시지 타입 = measurement / track / control
3. STM32 FCC측 (이번에 구현): 3-스레드 구조
   - UART RX 스레드: 프레임 수신·파싱 → k_msgq로 Tracking에 전달
   - Tracking 스레드: 게이팅 → 데이터연관(NN→GNN) → Kalman → M-of-N 트랙관리
   - Display 스레드: LVGL PPI 디스플레이 (트랙 테이블 read-only 접근)
   - 트랙 테이블은 mutex로 보호

## 진행 상태 (정직하게 유지)
- [x] PC Python 시뮬레이터 (프레이밍, FrameParser, RadarSensorModel, 유닛테스트)
- [x] STM32 앱 골격 (3-스레드)
- [x] UART 프레이밍 (보드측): frame.h/c (CRC16, encode, FrameParser), USART6 ISR+링버퍼
- [ ] Kalman 포팅 ← 다음 작업
- [ ] 트랙 관리 (M-of-N, 데이터 연관)
- [x] LVGL 상태판(텍스트): 최신 측정·프레임 카운터 LCD 표시
- [ ] LVGL PPI 디스플레이 (트랙 스코프) ← Kalman 이후
- [ ] HIL 통합 + 정량 검증

## UART 프레이밍 보드 검증 방법
1. 빌드·플래시 후 tio /dev/ttyACM0 으로 Zephyr 콘솔 연결
2. USB-UART 어댑터 Arduino D0(RX)/D1(TX) 연결 → /dev/ttyUSB0
3. `python run_sim.py --serial /dev/ttyUSB0 --scenario single_approach`
4. 콘솔에 `[tracking] #N t=... r=... az=...` 로그 보이면 RX 파이프라인 정상
5. run_sim.py 출력에 dummy TrackPayload(x=range, y=0) 수신되면 TX 정상

## 작업 원칙
- 작은 검증 사다리로 진행. 각 단계는 직전 단계 위에서 검증 가능해야 함.
  매 단계 끝에 "어떻게 보드에서 확인하는지"를 명시할 것.
- 한 번에 다 만들지 말 것. 골격 먼저, 살은 단계별로.
- 인라인 주석/문서는 한국어로.
- 인증 아티팩트(DO-178C 산출물 등)를 코드에서 자동 생성하지 말 것.
  그건 별도 트랙이고, 코드 자동생성은 안티패턴.
