#include <zephyr/ztest.h>
#include <math.h>
#include "kalman.h"

ZTEST_SUITE(kalman, NULL, NULL, NULL, NULL, NULL);

ZTEST(kalman, test_init_coordinate_convention)
{
    struct kalman_state ks;

    /* az=0 → 정면(북쪽): x=0, y=range */
    kalman_init(&ks, 1000.0f, 0.0f);
    zassert_within(ks.x[0], 0.0f, 1e-3f, "x should be 0 at az=0");
    zassert_within(ks.x[1], 1000.0f, 1e-3f, "y should equal range at az=0");

    /* az=π/2 → 동쪽: x=range, y=0 */
    kalman_init(&ks, 1000.0f, 1.57079632679f);
    zassert_within(ks.x[0], 1000.0f, 1.0f, "x should equal range at az=pi/2");
    zassert_within(ks.x[1], 0.0f, 1.0f, "y should be 0 at az=pi/2");
}

ZTEST(kalman, test_predict_propagates_constant_velocity)
{
    struct kalman_state ks;

    kalman_init(&ks, 1000.0f, 0.0f);  /* x=0, y=1000, vx=vy=0 */
    ks.x[2] = 20.0f;   /* vx */
    ks.x[3] = -5.0f;   /* vy */
    float p00_before = ks.P[0][0];

    kalman_predict(&ks, 2.0f);

    zassert_within(ks.x[0], 40.0f, 1e-3f, "x should advance by vx*dt");
    zassert_within(ks.x[1], 990.0f, 1e-3f, "y should advance by vy*dt");
    zassert_within(ks.x[2], 20.0f, 1e-3f, "vx should be unchanged by CV predict");
    zassert_within(ks.x[3], -5.0f, 1e-3f, "vy should be unchanged by CV predict");
    zassert_true(ks.P[0][0] > p00_before,
                 "position uncertainty should grow after predict");
}

ZTEST(kalman, test_converges_to_stationary_measurement)
{
    struct kalman_state ks;
    const float range = 5000.0f;
    const float az = 0.3f;

    kalman_init(&ks, range, az);
    for (int i = 0; i < 50; i++) {
        kalman_predict(&ks, 0.05f);
        kalman_update(&ks, range, az);
    }

    float expect_x = range * sinf(az);
    float expect_y = range * cosf(az);
    zassert_within(ks.x[0], expect_x, 1.0f, "x did not converge");
    zassert_within(ks.x[1], expect_y, 1.0f, "y did not converge");
    zassert_within(ks.x[2], 0.0f, 1.0f, "vx should settle near 0");
    zassert_within(ks.x[3], 0.0f, 1.0f, "vy should settle near 0");
}

ZTEST(kalman, test_azimuth_wrap_around)
{
    struct kalman_state ks;
    const float range = 1000.0f;
    const float az_true = 3.14f;    /* 거의 -y 방향, +π 근접 */
    const float az_meas = -3.14f;   /* wrap 경계 반대편이지만 실제 차이는 작음 */

    kalman_init(&ks, range, az_true);
    float y_before = ks.x[1];

    kalman_predict(&ks, 0.05f);
    kalman_update(&ks, range, az_meas);

    /* wrap을 안 했다면 혁신이 ~6.28rad이 되어 상태가 크게 튀어야 한다.
     * wrap이 맞다면 실제 각도 차이는 0.0032rad 수준이라 변화가 작아야 한다. */
    float y_after = ks.x[1];
    zassert_within(y_after, y_before, 50.0f,
                  "wrap-around bug: state jumped too much");
}

ZTEST(kalman, test_filtering_reduces_rmse_for_cv_target)
{
    struct kalman_state ks;
    const float dt = 0.1f;
    const int n_steps = 40;
    const int warmup = 10;
    const float x0 = 300.0f, y0 = 2000.0f, vx = 10.0f, vy = -5.0f;

    float r0  = sqrtf(x0 * x0 + y0 * y0);
    float az0 = atan2f(x0, y0);
    kalman_init(&ks, r0, az0);

    double raw_err2 = 0.0, filt_err2 = 0.0;
    int count = 0;

    for (int i = 1; i <= n_steps; i++) {
        float true_x = x0 + vx * (float)i * dt;
        float true_y = y0 + vy * (float)i * dt;
        float true_r = sqrtf(true_x * true_x + true_y * true_y);
        float true_az = atan2f(true_x, true_y);

        /* 결정론적 (재현 가능한) 가짜 노이즈: 부호가 주기적으로 바뀜 */
        float r_noise  = (i % 2 == 0) ?  10.0f : -10.0f;
        float az_noise = (i % 3 == 0) ?  0.01f : -0.01f;
        float r_meas   = true_r + r_noise;
        float az_meas  = true_az + az_noise;

        kalman_predict(&ks, dt);
        kalman_update(&ks, r_meas, az_meas);

        if (i > warmup) {
            float raw_x = r_meas * sinf(az_meas);
            float raw_y = r_meas * cosf(az_meas);

            raw_err2  += (double)((raw_x - true_x) * (raw_x - true_x)
                                 + (raw_y - true_y) * (raw_y - true_y));
            filt_err2 += (double)((ks.x[0] - true_x) * (ks.x[0] - true_x)
                                 + (ks.x[1] - true_y) * (ks.x[1] - true_y));
            count++;
        }
    }

    double raw_rmse  = sqrt(raw_err2 / count);
    double filt_rmse = sqrt(filt_err2 / count);

    zassert_true(filt_rmse < raw_rmse,
                "filtered RMSE(%.2f) should be lower than raw RMSE(%.2f)",
                filt_rmse, raw_rmse);
}
