# Kalman 필터 포팅 설계

## 배경

`fcc_app/src/tracking.c`는 현재 더미 에코(`x = range_m, y = 0`)만 보내고 있다.
`struct track`에는 "칼만 상태벡터(x,y,vx,vy), 공분산, M-of-N 카운터" TODO가 남아있다.
이번 단계는 실제 칼만 필터(EKF)를 포팅해 측정값을 받아 부드럽고 정확한 트랙 상태를
생성하는 것이다. 트랙 관리(M-of-N, 데이터 연관)는 다음 단계로 분리한다.

## 범위

- **단일 트랙만.** 데이터 연관/다중 트랙 관리는 다음 단계(트랙 관리)의 몫.
  들어오는 모든 측정값은 track 0 하나의 필터에 그대로 적용한다.
- 검증 시나리오는 클러터/미탐지를 끈 깨끗한 입력(`--pd 1.0 --clutter 0`)을 사용한다.
  클러터가 섞인 입력에서의 발산 방지는 트랙 관리 단계(게이팅)의 책임이다.

## 측정 모델: EKF (확장 칼만 필터)

상태 `[x, y, vx, vy]` (직교좌표, CV 모델), 측정 `[range_m, azimuth_rad]` (극좌표)는
비선형 관계이므로 EKF를 사용한다. (대안인 변환 측정 KF보다 구현이 복잡하지만,
이 프로젝트에서는 EKF를 정석으로 채택하기로 결정함.)

### 좌표계 컨벤션 (중요)

`pc_sim/targets.py`의 azimuth는 `atan2(x, y)` (북쪽 y축 기준 시계방향 양수)이며,
일반적인 `atan2(y, x)`가 아니다. 따라서:

- 극→직교: `x = r·sin(az)`, `y = r·cos(az)`
- 측정함수 `h(state) = [sqrt(x²+y²), atan2(x, y)]`
- 야코비안 H(2x4)도 이 컨벤션 기준으로 도출해야 한다.

## 행렬 연산

범용 NxM 행렬 라이브러리를 만들지 않는다. 차원이 고정(상태 4, 측정 2)이므로
`kalman.c` 안에 이 형태 전용의 고정 크기 함수를 직접 작성한다. 외부 의존성
(CMSIS-DSP 등) 없음 — 동적 할당 없음, 결정론적, 검증하기 쉬움.

## 파일 구조

```
fcc_app/src/kalman.h / kalman.c   신규: EKF 수학
fcc_app/src/tracking.h            struct track에 kalman_state, last_update_ms 추가
fcc_app/src/tracking.c            더미 에코 제거 → 실제 predict/update
fcc_app/tests/test_kalman/        신규: native_sim ztest
pc_sim/run_sim.py                 종료 시 RMSE 직서 출력 추가
```

```c
// kalman.h
struct kalman_state {
    float x[4];      /* [x_m, y_m, vx_mps, vy_mps] */
    float P[4][4];   /* 상태 공분산 */
};

void kalman_init(struct kalman_state *ks, float range_m, float azimuth_rad);
void kalman_predict(struct kalman_state *ks, float dt_s);
void kalman_update(struct kalman_state *ks, float range_m, float azimuth_rad);
```

## 알고리즘 상세

### 초기화 (`kalman_init`)

트랙의 첫 측정 수신 시 호출:

- `x = [r·sin(az), r·cos(az), 0, 0]` — 속도 정보 없으므로 0
- `P`: 위치 분산은 측정 노이즈(σ_range, σ_az)를 극→직교 변환 야코비안으로
  선형변환한 값. 속도 분산은 크게(예: 100² (m/s)²) 설정해 다음 업데이트가
  빠르게 수렴하도록 한다.

### 예측 (`kalman_predict(dt)`)

CV(등속) 모델:

- `x' = F·x` (F = 표준 등속 전이행렬, dt 기반)
- `P' = F·P·Fᵗ + Q(dt)` — Q는 discrete white-noise-acceleration 모델
  (dt⁴, dt³, dt² 항으로 구성). 가속도 분산 상수 `q`는 초기값으로 시작해
  RMSE 검증 결과를 보고 튜닝한다.
- `dt = (현재 timestamp_ms - track.last_update_ms) / 1000.0f`.
  고정 틱이 아니라 측정 도착 이벤트 기준.

### 갱신 (`kalman_update`)

- 혁신 `y = z - h(x)`. **azimuth 차이는 반드시 [-π, π]로 wrap**
  (안 하면 ±π 경계에서 발산).
- `S = H·P·Hᵗ + R`, `R = diag(σ_range², σ_az²)`.
  **σ_range=10m, σ_az=0.01rad — `pc_sim/targets.py`의 `RadarSensorModel`
  기본값과 반드시 일치시킬 것** (주석으로 명시).
- `K = P·Hᵗ·S⁻¹` (2x2 역행렬은 닫힌형 공식).
- `x += K·y`, `P = (I - K·H)·P`.

## 테스트 계획

### native_sim ztest (`fcc_app/tests/test_kalman/`)

보드 플래시 없이 호스트에서 검증. `west build -b native_sim fcc_app/tests/test_kalman`
+ `west build -t run`.

- `kalman_init` 후 상태가 좌표계 컨벤션대로 올바르게 변환되는지
- 정지 표적 + 반복 `update`만 호출 시 위치가 noise-free 측정값으로 수렴하는지
- 등속 표적 시뮬레이션(여러 step predict+update) 시 RMSE가 raw 측정 오차보다
  작은지 (필터링 효과 검증)
- azimuth wrap-around 경계(±π 근처)에서 혁신이 올바르게 계산되는지

### 보드 HIL 검증

```
python run_sim.py --serial /dev/ttyUSB0 --scenario single_approach --pd 1.0 --clutter 0
```

종료 시 출력될 RMSE가 작고 raw 측정 오차(σ_range=10m)보다 낮으면 필터링이
제대로 동작하는 것으로 판단한다.

### `run_sim.py` 변경

`_print_track`에서 매 트랙마다 `gt_err`를 리스트에 누적하고, `run()` 종료
(`finally`) 시 `RMSE = sqrt(mean(err²))`를 출력한다.

## 비범위 (다음 단계로 분리)

- 데이터 연관 (NN/GNN), 게이팅
- M-of-N 트랙 관리, 트랙 확정/삭제
- 다중 트랙 동시 처리
- LVGL 디스플레이 연동
