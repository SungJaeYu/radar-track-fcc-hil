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
