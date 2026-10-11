#include <math.h>
#include <stdint.h>
#include <string.h>

#include "angle_utils.h"
#include "gating.h"
#include "kalman_cv.h"
#include "radar_message.h"
#include "track.h"

#ifdef TRACK_TEST_HOST
#include <stdio.h>
#include <stdlib.h>
#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
        exit(EXIT_FAILURE); \
    } \
} while (0)
#define TEST(name) static void name(void)
#else
#include <zephyr/ztest.h>
#define CHECK(condition) zassert_true((condition), "%s", #condition)
#define TEST(name) ZTEST(tracking, name)
ZTEST_SUITE(tracking, NULL, NULL, NULL, NULL, NULL);
#endif

#define PI_F 3.14159265358979323846f
#define RAD(degrees) ((degrees) * PI_F / 180.0f)
#define NEAR(actual, expected) CHECK(fabsf((actual) - (expected)) < 1e-5f)

static struct track new_track(uint32_t timestamp)
{
    struct track t = {0};
    track_init(&t);
    track_start(&t, 1000.0f, RAD(30.0f), timestamp);
    return t;
}

static void hit(struct track *t, uint32_t timestamp)
{
    CHECK(track_predict(t, timestamp));
    CHECK(gating_check(1000.0f, t->range_filter.estimate,
                       RAD(30.0f), t->azimuth_filter.estimate));
    track_hit(t, 1000.0f, RAD(30.0f));
}

TEST(test_angle_wrap)
{
    NEAR(wrap_angle_rad(RAD(181.0f)), RAD(-179.0f));
    NEAR(wrap_angle_rad(RAD(-181.0f)), RAD(179.0f));
    NEAR(wrap_angle_rad(-PI_F), PI_F);
    NEAR(angle_diff_rad(RAD(-179.0f), RAD(179.0f)), RAD(2.0f));
    NEAR(angle_diff_rad(RAD(179.0f), RAD(-179.0f)), RAD(-2.0f));
}

TEST(test_gate)
{
    CHECK(gating_check(1100, 1000, RAD(9), 0));
    CHECK(!gating_check(1101, 1000, 0, 0));
    CHECK(!gating_check(899, 1000, 0, 0));
    CHECK(!gating_check(1000, 1000, RAD(11), 0));
    CHECK(!gating_check(1000, 1000, RAD(-11), 0));
    CHECK(gating_check(1000, 1000, RAD(-179), RAD(179)));
}

TEST(test_cv_prediction_and_covariance)
{
    struct kalman_cv_filter k;
    kalman_cv_init(&k, 10, 2, 4, 0, 0, 9, 4, 1);
    kalman_cv_predict(&k, 0.5f);
    NEAR(k.estimate, 11); NEAR(k.rate, 2);
    // Hand-calculated F P F^T + Q for dt=0.5, acceleration variance=4.
    NEAR(k.p00, 6.3125f); NEAR(k.p01, 4.75f);
    NEAR(k.p10, 4.75f); NEAR(k.p11, 10);
}

TEST(test_cv_correction)
{
    struct kalman_cv_filter k;
    kalman_cv_init(&k, 10, 2, 4, 1, 1, 9, 0, 4);
    kalman_cv_correct(&k, 14);
    NEAR(k.estimate, 12); NEAR(k.rate, 2.5f);
    NEAR(k.p00, 2); NEAR(k.p01, 0.5f);
    NEAR(k.p10, 0.5f); NEAR(k.p11, 8.875f);
}

TEST(test_angle_prediction_and_correction)
{
    struct kalman_cv_filter k;
    kalman_cv_init(&k, RAD(179), RAD(4), 1, 0, 0, 1, 0, 1);
    kalman_cv_predict_angle(&k, 1);
    NEAR(k.estimate, RAD(-177));
    kalman_cv_init(&k, RAD(179), 0, 1, 0, 0, 1, 0, 1);
    kalman_cv_correct_angle(&k, RAD(-177));
    NEAR(k.estimate, RAD(-179));
    NEAR(k.rate, 0);
}

TEST(test_stationary_convergence)
{
    struct kalman_cv_filter k;
    kalman_cv_init(&k, 990, 0, 25, 0, 0, 100, 1, 4);
    for (int i = 0; i < 200; ++i) {
        kalman_cv_predict(&k, 0.1f);
        kalman_cv_correct(&k, 1000);
        CHECK(isfinite(k.estimate) && k.p00 >= 0 && k.p11 >= 0);
        NEAR(k.p01, k.p10);
    }
    CHECK(fabsf(k.estimate - 1000) < 0.1f);
    CHECK(fabsf(k.rate) < 0.1f);
}

TEST(test_decode_semicircle)
{
    uint8_t payload[] = {0, 0, 0, 100, 0x03, 0xe8, 0x40, 0, 0, 0};
    struct radar_measurement m;
    CHECK(decode_radar_measurement(payload, sizeof payload, &m));
    CHECK(m.timestamp_ms == 100); NEAR(m.range_m, 1000);
    NEAR(m.azimuth_rad, PI_F / 2); CHECK(validate_radar_measurement(&m));
    payload[6] = 0xc0;
    CHECK(decode_radar_measurement(payload, sizeof payload, &m));
    NEAR(m.azimuth_rad, -PI_F / 2);
    payload[6] = 0x80;
    CHECK(decode_radar_measurement(payload, sizeof payload, &m));
    NEAR(m.azimuth_rad, -PI_F); CHECK(validate_radar_measurement(&m));
    payload[6] = 0x7f; payload[7] = payload[8] = payload[9] = 0xff;
    CHECK(decode_radar_measurement(payload, sizeof payload, &m));
    NEAR(m.azimuth_rad, PI_F); CHECK(validate_radar_measurement(&m));
    CHECK(!decode_radar_measurement(payload, 8, &m));
    CHECK(!decode_radar_measurement(NULL, 10, &m));
    CHECK(!decode_radar_measurement(payload, 10, NULL));
    m.range_m = 0; CHECK(!validate_radar_measurement(&m));
}

TEST(test_timestamp_rejection)
{
    const uint32_t rejected[] = {100, 99, 100 + UINT32_C(0x80000000)};
    for (unsigned i = 0; i < sizeof rejected / sizeof rejected[0]; ++i) {
        struct track t = new_track(100);
        unsigned char before[sizeof t];
        memcpy(before, &t, sizeof t);
        CHECK(!track_predict(&t, rejected[i]));
        CHECK(memcmp(before, &t, sizeof t) == 0);
    }
}

TEST(test_elapsed_time_and_wraparound)
{
    struct track t = new_track(100);
    t.range_filter.rate = 20;
    CHECK(track_predict(&t, 400)); NEAR(t.range_filter.estimate, 1006);
    CHECK(t.last_measurement_time_ms == 400);
    t = new_track(UINT32_MAX - 49);
    t.range_filter.rate = 20;
    CHECK(track_predict(&t, 50)); NEAR(t.range_filter.estimate, 1002);
    CHECK(t.last_measurement_time_ms == 50);
}

TEST(test_long_gap_and_restart)
{
    struct track t = new_track(100);
    CHECK(track_predict(&t, 1100));
    t = new_track(100);
    CHECK(!track_predict(&t, 1101)); CHECK(t.state == TRACK_EMPTY);
    CHECK(t.hit_count == 0 && t.miss_count == 0);
    track_start(&t, 2000, RAD(-30), 1200);
    CHECK(t.state == TRACK_TENTATIVE && t.hit_count == 1);
    NEAR(t.range_filter.estimate, 2000); NEAR(t.range_filter.rate, 0);
    NEAR(t.azimuth_filter.estimate, RAD(-30));
}

TEST(test_confirmation_coasting_and_loss)
{
    struct track t = new_track(0);
    hit(&t, 100); CHECK(t.state == TRACK_TENTATIVE);
    hit(&t, 200); CHECK(t.state == TRACK_CONFIRMED);
    CHECK(track_predict(&t, 300)); track_miss(&t);
    CHECK(t.state == TRACK_COASTING && t.hit_count == 0 && t.miss_count == 1);
    hit(&t, 400); CHECK(t.state == TRACK_CONFIRMED && t.miss_count == 0);
    for (unsigned i = 1; i <= 5; ++i) {
        CHECK(track_predict(&t, 400 + i * 100)); track_miss(&t);
        CHECK(t.state == (i < 5 ? TRACK_COASTING : TRACK_LOST));
    }
}

TEST(test_tentative_miss_and_inactive_calls)
{
    struct track t = new_track(0);
    track_miss(&t);
    CHECK(t.state == TRACK_EMPTY && t.hit_count == 0 && t.miss_count == 0);
    unsigned char before[sizeof t];
    memcpy(before, &t, sizeof t);
    track_hit(&t, 500, 0); track_miss(&t);
    CHECK(!track_predict(&t, 100)); CHECK(memcmp(before, &t, sizeof t) == 0);
}

TEST(test_lost_restarts_for_any_timestamp)
{
    const uint32_t timestamps[] = {0, 900, 1000};
    for (unsigned i = 0; i < sizeof timestamps / sizeof timestamps[0]; ++i) {
        struct track t = new_track(0);
        hit(&t, 100); hit(&t, 200);
        for (unsigned n = 1; n <= 5; ++n) {
            CHECK(track_predict(&t, 400 + n * 100)); track_miss(&t);
        }
        CHECK(t.state == TRACK_LOST);
        unsigned char before[sizeof t];
        memcpy(before, &t, sizeof t);
        track_hit(&t, 500, 0); track_miss(&t);
        CHECK(memcmp(before, &t, sizeof t) == 0);
        CHECK(!track_predict(&t, timestamps[i]));
        CHECK(t.state == TRACK_EMPTY && t.hit_count == 0 && t.miss_count == 0);
        track_start(&t, 500, 0, timestamps[i] + 100);
        CHECK(t.state == TRACK_TENTATIVE && t.hit_count == 1);
        NEAR(t.range_filter.estimate, 500); NEAR(t.range_filter.rate, 0);
    }
}

#ifdef TRACK_TEST_HOST
int main(void)
{
#define RUN(name) do { name(); puts("PASS: " #name); } while (0)
    RUN(test_angle_wrap);
    RUN(test_gate);
    RUN(test_cv_prediction_and_covariance);
    RUN(test_cv_correction);
    RUN(test_angle_prediction_and_correction);
    RUN(test_stationary_convergence);
    RUN(test_decode_semicircle);
    RUN(test_timestamp_rejection);
    RUN(test_elapsed_time_and_wraparound);
    RUN(test_long_gap_and_restart);
    RUN(test_confirmation_coasting_and_loss);
    RUN(test_tentative_miss_and_inactive_calls);
    RUN(test_lost_restarts_for_any_timestamp);
    return 0;
}
#endif
