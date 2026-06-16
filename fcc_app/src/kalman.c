/*
 * kalman.c — 단일 트랙 EKF: 상태 [x,y,vx,vy] (CV), 측정 [range_m, azimuth_rad]
 *
 * 좌표계: pc_sim/targets.py와 동일. azimuth = atan2(x, y).
 * R(측정 노이즈)은 pc_sim/targets.py의 RadarSensorModel 기본값과
 * 반드시 일치시킬 것: sigma_range_m=10.0, sigma_azimuth_rad=0.01
 */

#include "kalman.h"
#include <math.h>
#include <string.h>

#define SIGMA_RANGE_M     10.0f
#define SIGMA_AZIMUTH_RAD 0.01f

/* 첫 측정만으로는 속도를 모르므로 초기 속도 분산을 크게 잡는다 (m/s)^2 */
#define INIT_VEL_VAR      10000.0f

void kalman_init(struct kalman_state *ks, float range_m, float azimuth_rad)
{
    float sin_az = sinf(azimuth_rad);
    float cos_az = cosf(azimuth_rad);

    ks->x[0] = range_m * sin_az;   /* x */
    ks->x[1] = range_m * cos_az;   /* y */
    ks->x[2] = 0.0f;               /* vx */
    ks->x[3] = 0.0f;               /* vy */

    /* 극→직교 변환 야코비안으로 위치 분산 근사 (range/az 측정오차 독립 가정) */
    float var_r  = SIGMA_RANGE_M * SIGMA_RANGE_M;
    float var_az = SIGMA_AZIMUTH_RAD * SIGMA_AZIMUTH_RAD;

    float var_x = (sin_az * sin_az) * var_r
                + (range_m * cos_az) * (range_m * cos_az) * var_az;
    float var_y = (cos_az * cos_az) * var_r
                + (range_m * sin_az) * (range_m * sin_az) * var_az;
    float cov_xy = sin_az * cos_az * var_r
                 - range_m * range_m * sin_az * cos_az * var_az;

    memset(ks->P, 0, sizeof(ks->P));
    ks->P[0][0] = var_x;
    ks->P[1][1] = var_y;
    ks->P[0][1] = cov_xy;
    ks->P[1][0] = cov_xy;
    ks->P[2][2] = INIT_VEL_VAR;
    ks->P[3][3] = INIT_VEL_VAR;
}
