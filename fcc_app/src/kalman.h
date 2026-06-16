#pragma once
/*
 * kalman.h — 단일 트랙 EKF 공개 인터페이스
 *
 * 상태: [x_m, y_m, vx_mps, vy_mps]  (CV 등속 모델, 직교좌표)
 * 측정: [range_m, azimuth_rad]      (극좌표)
 *
 * 좌표계: pc_sim/targets.py와 동일. azimuth = atan2(x, y)
 *         (북쪽 y축 기준 시계방향 양수, 일반적인 atan2(y,x)가 아님)
 *         극→직교: x = r*sin(az), y = r*cos(az)
 */

struct kalman_state {
    float x[4];      /* [x_m, y_m, vx_mps, vy_mps] */
    float P[4][4];   /* 상태 공분산 */
};

/* 첫 측정으로 상태 초기화. 속도=0, 위치 분산은 극→직교 변환 야코비안으로 근사. */
void kalman_init(struct kalman_state *ks, float range_m, float azimuth_rad);

/* CV 모델로 dt_s초 예측 전진 (상태 + 공분산). */
void kalman_predict(struct kalman_state *ks, float dt_s);

/* 극좌표 측정으로 EKF 갱신. */
void kalman_update(struct kalman_state *ks, float range_m, float azimuth_rad);
