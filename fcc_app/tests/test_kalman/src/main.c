#include <zephyr/ztest.h>
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
