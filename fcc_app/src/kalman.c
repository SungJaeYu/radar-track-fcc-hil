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

/* CV 모델 가속도 잡음 강도 (튜닝 대상, 초기값) */
#define PROCESS_NOISE_Q   1.0f

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

static void mat4_mul(const float a[4][4], const float b[4][4], float out[4][4])
{
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            float sum = 0.0f;
            for (int k = 0; k < 4; k++) {
                sum += a[i][k] * b[k][j];
            }
            out[i][j] = sum;
        }
    }
}

static void mat4_transpose(const float a[4][4], float out[4][4])
{
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            out[j][i] = a[i][j];
        }
    }
}

static void mat4_add(const float a[4][4], const float b[4][4], float out[4][4])
{
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            out[i][j] = a[i][j] + b[i][j];
        }
    }
}

void kalman_predict(struct kalman_state *ks, float dt_s)
{
    float dt = dt_s;

    /* x' = F x  (CV 모델) */
    ks->x[0] += ks->x[2] * dt;
    ks->x[1] += ks->x[3] * dt;
    /* vx, vy 불변 */

    float F[4][4] = {
        {1.0f, 0.0f, dt,   0.0f},
        {0.0f, 1.0f, 0.0f, dt  },
        {0.0f, 0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 0.0f, 1.0f},
    };
    float Ft[4][4];
    float FP[4][4];
    float FPFt[4][4];

    mat4_transpose(F, Ft);
    mat4_mul(F, ks->P, FP);
    mat4_mul(FP, Ft, FPFt);

    /* discrete white-noise-acceleration 모델 Q (x, y축 독립) */
    float dt2 = dt * dt;
    float dt3 = dt2 * dt;
    float dt4 = dt3 * dt;
    float q = PROCESS_NOISE_Q;

    float Q[4][4] = {
        {q * dt4 / 4.0f, 0.0f,            q * dt3 / 2.0f, 0.0f          },
        {0.0f,           q * dt4 / 4.0f,  0.0f,           q * dt3 / 2.0f},
        {q * dt3 / 2.0f, 0.0f,            q * dt2,        0.0f          },
        {0.0f,           q * dt3 / 2.0f,  0.0f,           q * dt2       },
    };

    mat4_add(FPFt, Q, ks->P);
}

/* azimuth 차이를 [-pi, pi]로 wrap */
static float wrap_angle(float a)
{
    const float pi = 3.14159265358979323846f;

    while (a > pi) {
        a -= 2.0f * pi;
    }
    while (a < -pi) {
        a += 2.0f * pi;
    }
    return a;
}

void kalman_update(struct kalman_state *ks, float range_m, float azimuth_rad)
{
    float x = ks->x[0];
    float y = ks->x[1];
    float r2 = x * x + y * y;
    float r  = sqrtf(r2);

    if (r < 1.0f) {
        r  = 1.0f;   /* 원점 근처 발산 방지 */
        r2 = 1.0f;
    }

    /* 야코비안 H (2x4): h(x) = [range, az] = [sqrt(x^2+y^2), atan2(x, y)] */
    float H[2][4] = {
        { x / r,   y / r,   0.0f, 0.0f },
        { y / r2, -x / r2,  0.0f, 0.0f },
    };

    float range_hat = r;
    float az_hat     = atan2f(x, y);
    float innov[2] = {
        range_m - range_hat,
        wrap_angle(azimuth_rad - az_hat),
    };

    float R[2][2] = {
        { SIGMA_RANGE_M * SIGMA_RANGE_M, 0.0f },
        { 0.0f, SIGMA_AZIMUTH_RAD * SIGMA_AZIMUTH_RAD },
    };

    /* PHt = P * H^T  (4x4 * 4x2 = 4x2) */
    float PHt[4][2];
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 2; j++) {
            float sum = 0.0f;
            for (int k = 0; k < 4; k++) {
                sum += ks->P[i][k] * H[j][k];
            }
            PHt[i][j] = sum;
        }
    }

    /* S = H * PHt + R  (2x4 * 4x2 = 2x2) */
    float S[2][2];
    for (int i = 0; i < 2; i++) {
        for (int j = 0; j < 2; j++) {
            float sum = 0.0f;
            for (int k = 0; k < 4; k++) {
                sum += H[i][k] * PHt[k][j];
            }
            S[i][j] = sum + R[i][j];
        }
    }

    /* S 역행렬 (2x2 닫힌형) */
    float det = S[0][0] * S[1][1] - S[0][1] * S[1][0];
    if (fabsf(det) < 1e-9f) {
        return;   /* 특이행렬: 이번 업데이트 건너뜀 */
    }
    float inv_det = 1.0f / det;
    float Sinv[2][2] = {
        {  S[1][1] * inv_det, -S[0][1] * inv_det },
        { -S[1][0] * inv_det,  S[0][0] * inv_det },
    };

    /* K = PHt * Sinv  (4x2 * 2x2 = 4x2) */
    float K[4][2];
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 2; j++) {
            K[i][j] = PHt[i][0] * Sinv[0][j] + PHt[i][1] * Sinv[1][j];
        }
    }

    /* x += K * innov */
    for (int i = 0; i < 4; i++) {
        ks->x[i] += K[i][0] * innov[0] + K[i][1] * innov[1];
    }

    /* P = (I - K H) P */
    float KH[4][4];
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            KH[i][j] = K[i][0] * H[0][j] + K[i][1] * H[1][j];
        }
    }
    float KHP[4][4];
    mat4_mul(KH, ks->P, KHP);
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            ks->P[i][j] -= KHP[i][j];
        }
    }
}
