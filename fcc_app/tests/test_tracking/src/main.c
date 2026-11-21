#include <float.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "tracking/angle_utils.h"
#include "tracking/gating.h"
#include "tracking/kalman_cv.h"
#include "protocol/measurement_message.h"
#include "tracking/track.h"

#ifdef TRACK_TEST_HOST
#include "../../common/test_runner.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition);                        \
            exit(EXIT_FAILURE);                                                                    \
        }                                                                                          \
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

static struct track new_track(uint32_t sensor_time_ms)
{
    struct track t = {0};
    track_init(&t);
    track_start(&t, 1000.0f, RAD(30.0f), sensor_time_ms);
    return t;
}

// Verify input preservation before the caller explicitly applies the next state.
static struct track predict_checked(const struct track *p_current, uint32_t sensor_time_ms)
{
    unsigned char before[sizeof *p_current];
    memcpy(before, p_current, sizeof before);
    const struct track next = track_predict_next(p_current, sensor_time_ms);
    CHECK(memcmp(before, p_current, sizeof before) == 0);
    CHECK(next.state == p_current->state);
    CHECK(next.hit_count == p_current->hit_count);
    CHECK(next.miss_count == p_current->miss_count);
    return next;
}

static void hit(struct track *p_track, uint32_t sensor_time_ms)
{
    *p_track = predict_checked(p_track, sensor_time_ms);
    const struct estimator_output predicted = track_get_estimate(p_track);
    CHECK(gating_check(1000.0f, predicted.range_m, RAD(30.0f), predicted.azimuth_rad));
    track_apply_measurement(p_track, 1000.0f, RAD(30.0f));
}

TEST(test_angle_wrap)
{
    NEAR(wrap_angle_rad(RAD(181.0f)), RAD(-179.0f));
    NEAR(wrap_angle_rad(RAD(-181.0f)), RAD(179.0f));
    NEAR(wrap_angle_rad(-PI_F), PI_F);
    NEAR(angle_diff_rad(RAD(-179.0f), RAD(179.0f)), RAD(2.0f));
    NEAR(angle_diff_rad(RAD(179.0f), RAD(-179.0f)), RAD(-2.0f));
}

TEST(test_angle_wrap_boundaries)
{
    CHECK(wrap_angle_rad(0.0f) == 0.0f);
    CHECK(wrap_angle_rad(-0.0f) == 0.0f);
    CHECK(wrap_angle_rad(PI_F) == PI_F);
    CHECK(wrap_angle_rad(-PI_F) == PI_F);
    CHECK(wrap_angle_rad(2.0f * PI_F) == 0.0f);
    CHECK(wrap_angle_rad(-2.0f * PI_F) == 0.0f);

    // Adjacent representable values exercise the open and closed endpoints.
    const float inside = nextafterf(PI_F, 0.0f);
    const float outside = nextafterf(PI_F, INFINITY);
    CHECK(wrap_angle_rad(inside) == inside);
    CHECK(wrap_angle_rad(-inside) == -inside);
    CHECK(wrap_angle_rad(outside) < 0.0f);
    CHECK(wrap_angle_rad(-outside) > 0.0f);
}

TEST(test_angle_wrap_multiple_turns)
{
    for (int turns = -10; turns <= 10; ++turns) {
        NEAR(wrap_angle_rad((float)turns * 2.0f * PI_F + 0.25f), 0.25f);
        NEAR(wrap_angle_rad((float)turns * 2.0f * PI_F - 0.25f), -0.25f);
    }
}

TEST(test_angle_wrap_large_finite_values)
{
    // Check termination and range, not a physically meaningful angle at this precision.
    const float values[] = {1e10f, -1e10f, FLT_MAX, -FLT_MAX};
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
        const float wrapped = wrap_angle_rad(values[i]);
        CHECK(isfinite(wrapped));
        CHECK(wrapped > -PI_F && wrapped <= PI_F);
        CHECK(wrap_angle_rad(wrapped) == wrapped);
    }
}

TEST(test_correction_rejects_invalid_innovation_covariance)
{
    const float values[] = {0.0f, -1.0f, 1e-7f, 1e-6f, NAN, INFINITY, -INFINITY};
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
        struct kalman_cv_filter filter;
        kalman_cv_init(&filter, 10, 2, 1, 0, 0, 1, 0, 1);
        // Inject internal corruption after valid initialization; exercise the release guard.
        filter.p00 = values[i];
        filter.measurement_noise_variance = 0.0f;
        unsigned char before[sizeof(filter)];
        memcpy(before, &filter, sizeof(filter));
        kalman_cv_correct(&filter, 12);
        CHECK(memcmp(before, &filter, sizeof(filter)) == 0);
        kalman_cv_correct_angle(&filter, 0.1f);
        CHECK(memcmp(before, &filter, sizeof(filter)) == 0);
    }
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
    NEAR(k.estimate, 11);
    NEAR(k.rate, 2);
    // Hand-calculated F P F^T + Q for interval_s=0.5, acceleration variance=4.
    NEAR(k.p00, 6.3125f);
    NEAR(k.p01, 4.75f);
    NEAR(k.p10, 4.75f);
    NEAR(k.p11, 10);
}

TEST(test_cv_correction)
{
    struct kalman_cv_filter k;
    kalman_cv_init(&k, 10, 2, 4, 1, 1, 9, 0, 4);
    kalman_cv_correct(&k, 14);
    NEAR(k.estimate, 12);
    NEAR(k.rate, 2.5f);
    NEAR(k.p00, 2);
    NEAR(k.p01, 0.5f);
    NEAR(k.p10, 0.5f);
    NEAR(k.p11, 8.875f);
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
    CHECK(measurement_message_decode(payload, sizeof payload, &m));
    CHECK(m.sensor_time_ms == 100);
    NEAR(m.range_m, 1000);
    NEAR(m.azimuth_rad, PI_F / 2);
    CHECK(measurement_message_validate(&m));
    payload[6] = 0xc0;
    CHECK(measurement_message_decode(payload, sizeof payload, &m));
    NEAR(m.azimuth_rad, -PI_F / 2);
    payload[6] = 0x80;
    CHECK(measurement_message_decode(payload, sizeof payload, &m));
    NEAR(m.azimuth_rad, -PI_F);
    CHECK(measurement_message_validate(&m));
    payload[6] = 0x7f;
    payload[7] = payload[8] = payload[9] = 0xff;
    CHECK(measurement_message_decode(payload, sizeof payload, &m));
    NEAR(m.azimuth_rad, PI_F);
    CHECK(measurement_message_validate(&m));
    CHECK(!measurement_message_decode(payload, 8, &m));
    CHECK(!measurement_message_decode(NULL, 10, &m));
    CHECK(!measurement_message_decode(payload, 10, NULL));
    m.range_m = 0;
    CHECK(!measurement_message_validate(&m));
}

TEST(test_prediction_preserves_lifecycle)
{
    const TrackState states[] = {TRACK_STATE_TENTATIVE, TRACK_STATE_CONFIRMED, TRACK_STATE_COASTING};
    for (size_t i = 0; i < sizeof states / sizeof states[0]; ++i) {
        struct track current = new_track(100);
        current.state = states[i];
        current.hit_count = i == 0 ? 1 : i == 1 ? 3 : 0;
        current.miss_count = i == 2 ? 2 : 0;
        current.estimator.range_filter.rate = 20.0f;
        const struct track next = predict_checked(&current, 1100);
        CHECK(next.estimate_sensor_ms == 1100);
        NEAR(next.estimator.range_filter.estimate, 1020.0f);
        // Independent one-second CV covariance expectations with the initial tuning.
        NEAR(next.estimator.range_filter.p00, 10050.0f);
        NEAR(next.estimator.range_filter.p01, 10050.0f);
        NEAR(next.estimator.range_filter.p10, 10050.0f);
        NEAR(next.estimator.range_filter.p11, 10100.0f);
        CHECK(next.estimator.range_filter.process_noise_variance == 100.0f);
        CHECK(next.estimator.range_filter.measurement_noise_variance == 25.0f);
        NEAR(next.estimator.azimuth_filter.estimate, RAD(30.0f));
        CHECK(next.estimator.azimuth_filter.p00 > current.estimator.azimuth_filter.p00);
    }
}


TEST(test_elapsed_time_and_wraparound)
{
    struct track t = new_track(100);
    // Inject a known velocity to isolate timestamp handling from filter convergence.
    t.estimator.range_filter.rate = 20;
    t = predict_checked(&t, 400);
    NEAR(track_get_estimate(&t).range_m, 1006);
    CHECK(t.estimate_sensor_ms == 400);
    t = new_track(UINT32_MAX - 49);
    t.estimator.range_filter.rate = 20;
    t = predict_checked(&t, 50);
    NEAR(track_get_estimate(&t).range_m, 1002);
    CHECK(t.estimate_sensor_ms == 50);
}

TEST(test_track_restart)
{
    struct track t = new_track(100);
    t = predict_checked(&t, 1100);
    track_init(&t);
    CHECK(t.state == TRACK_STATE_EMPTY);
    CHECK(t.hit_count == 0 && t.miss_count == 0);
    track_start(&t, 2000, RAD(-30), 1200);
    CHECK(t.state == TRACK_STATE_TENTATIVE && t.hit_count == 1);
    const struct estimator_output restarted = track_get_estimate(&t);
    NEAR(restarted.range_m, 2000);
    NEAR(restarted.range_rate_mps, 0);
    NEAR(restarted.azimuth_rad, RAD(-30));
}


TEST(test_confirmation_coasting_and_loss)
{
    struct track t = new_track(0);
    hit(&t, 100);
    CHECK(t.state == TRACK_STATE_TENTATIVE);
    hit(&t, 200);
    CHECK(t.state == TRACK_STATE_CONFIRMED);
    t = predict_checked(&t, 300);
    track_apply_miss(&t);
    CHECK(t.state == TRACK_STATE_COASTING && t.hit_count == 0 && t.miss_count == 1);
    hit(&t, 400);
    CHECK(t.state == TRACK_STATE_CONFIRMED && t.miss_count == 0);
    for (unsigned i = 1; i <= 5; ++i) {
        t = predict_checked(&t, 400 + i * 100);
        track_apply_miss(&t);
        CHECK(t.state == (i < 5 ? TRACK_STATE_COASTING : TRACK_STATE_LOST));
    }
}

TEST(test_tentative_miss_and_inactive_calls)
{
    struct track t = new_track(0);
    track_apply_miss(&t);
    CHECK(t.state == TRACK_STATE_EMPTY && t.hit_count == 0 && t.miss_count == 0);
    unsigned char before[sizeof t];
    memcpy(before, &t, sizeof t);
    track_apply_measurement(&t, 500, 0);
    track_apply_miss(&t);
    CHECK(memcmp(before, &t, sizeof t) == 0);
}

TEST(test_lost_inactive_calls_and_restart)
{
    struct track t = new_track(0);
    hit(&t, 100);
    hit(&t, 200);
    for (unsigned n = 1; n <= 5; ++n) {
        t = predict_checked(&t, 400 + n * 100);
        track_apply_miss(&t);
    }
    CHECK(t.state == TRACK_STATE_LOST);
    unsigned char before[sizeof t];
    memcpy(before, &t, sizeof t);
    track_apply_measurement(&t, 500, 0);
    track_apply_miss(&t);
    CHECK(memcmp(before, &t, sizeof t) == 0);
    track_init(&t);
    CHECK(t.state == TRACK_STATE_EMPTY && t.hit_count == 0 && t.miss_count == 0);
    track_start(&t, 500, 0, 100);
    CHECK(t.state == TRACK_STATE_TENTATIVE && t.hit_count == 1);
    const struct estimator_output restarted = track_get_estimate(&t);
    NEAR(restarted.range_m, 500);
    NEAR(restarted.range_rate_mps, 0);
}


TEST(test_measurement_wire_domain_matrix)
{
    const uint32_t sensor_timestamps_ms[] = {0, 1, UINT32_MAX - 1, UINT32_MAX};
    const uint16_t ranges[] = {0, 1, 2, UINT16_MAX - 1, UINT16_MAX};
    const int32_t angles[] = {INT32_MIN, INT32_MIN + 1, -1, 0, 1, INT32_MAX - 1, INT32_MAX};
    for (size_t ti = 0; ti < sizeof(sensor_timestamps_ms) / sizeof(sensor_timestamps_ms[0]); ++ti) {
        for (size_t ri = 0; ri < sizeof(ranges) / sizeof(ranges[0]); ++ri) {
            for (size_t ai = 0; ai < sizeof(angles) / sizeof(angles[0]); ++ai) {
                uint32_t sensor_time_ms = sensor_timestamps_ms[ti], angle = (uint32_t)angles[ai];
                uint16_t range = ranges[ri];
                uint8_t payload[] = {sensor_time_ms >> 24, sensor_time_ms >> 16, sensor_time_ms >> 8, sensor_time_ms,
                    range >> 8, range, angle >> 24, angle >> 16, angle >> 8, angle};
                struct radar_measurement measurement;
                CHECK(measurement_message_decode(payload, sizeof(payload), &measurement));
                CHECK(measurement.sensor_time_ms == sensor_time_ms);
                CHECK(measurement.range_m == (float)range);
                CHECK(fabs((double)measurement.azimuth_rad -
                           (double)angles[ai] * 3.14159265358979323846 / 2147483648.0) < 3e-7);
                CHECK(measurement_message_validate(&measurement) == (range != 0));
            }
        }
    }
}

TEST(test_measurement_length_domain_preserves_output)
{
    const uint8_t payload[UINT8_MAX] = {0};
    for (unsigned length = 0; length <= UINT8_MAX; ++length) {
        if (length == 10) {
            continue;
        }
        struct radar_measurement measurement = {.sensor_time_ms = 123, .range_m = 456, .azimuth_rad = .5f};
        unsigned char before[sizeof(measurement)];
        memcpy(before, &measurement, sizeof(measurement));
        CHECK(!measurement_message_decode(payload, (uint8_t)length, &measurement));
        CHECK(memcmp(before, &measurement, sizeof(measurement)) == 0);
    }
}

TEST(test_prediction_interval_and_noise_matrix)
{
    const float intervals[] = {0, .001f, .02f, .1f, 1};
    const float noises[] = {0, 1, 64, 900};
    const float speeds[] = {-5, 0, 5};
    for (size_t i = 0; i < sizeof(intervals) / sizeof(intervals[0]); ++i) {
        for (size_t j = 0; j < sizeof(noises) / sizeof(noises[0]); ++j) {
            for (size_t k = 0; k < sizeof(speeds) / sizeof(speeds[0]); ++k) {
                struct kalman_cv_filter filter;
                kalman_cv_init(&filter, 1000, speeds[k], 4, 0, 0, 9, noises[j], 1);
                kalman_cv_predict(&filter, intervals[i]);
                // Double-precision diagonal-P reference, independent of in-place update order.
                double interval_s = intervals[i], q = noises[j];
                CHECK(fabs(filter.estimate - (1000.0 + speeds[k] * interval_s)) < 1e-4);
                CHECK(filter.rate == speeds[k]);
                CHECK(fabs(filter.p00 - (4 + 9 * interval_s * interval_s + q * pow(interval_s, 4) / 4)) < 1e-4);
                CHECK(fabs(filter.p01 - (9 * interval_s + q * pow(interval_s, 3) / 2)) < 1e-4);
                CHECK(fabs(filter.p11 - (9 + q * interval_s * interval_s)) < 1e-4);
                CHECK(filter.p01 == filter.p10);
                CHECK((double)filter.p00 * filter.p11 - (double)filter.p01 * filter.p10 >= -1e-4);
            }
        }
    }
}

TEST(test_correction_noise_and_residual_matrix)
{
    const float noises[] = {.25f, 1, 4, 64, 900};
    const float measurements[] = {1, 970, 1000, 1030, 65535};
    for (size_t i = 0; i < sizeof(noises) / sizeof(noises[0]); ++i) {
        for (size_t j = 0; j < sizeof(measurements) / sizeof(measurements[0]); ++j) {
            struct kalman_cv_filter filter;
            kalman_cv_init(&filter, 1000, 0, 4, 0, 0, 9, 0, noises[i]);
            kalman_cv_correct(&filter, measurements[j]);
            double gain = 4.0 / (4.0 + noises[i]);
            double expected = (1 - gain) * 1000 + gain * measurements[j];
            CHECK(fabs(filter.estimate - expected) < .01);
            CHECK(filter.estimate >= fminf(1000, measurements[j]));
            CHECK(filter.estimate <= fmaxf(1000, measurements[j]));
            CHECK(fabs(filter.p00 - 4 * noises[i] / (4 + noises[i])) < 1e-5);
            CHECK(filter.rate == 0 && filter.p11 == 9 && filter.p01 == 0 && filter.p10 == 0);
        }
    }
    struct kalman_cv_filter filter;
    kalman_cv_init(&filter, 10, 0, nextafterf(1e-6f, INFINITY), 0, 0, 1, 0, 1);
    filter.measurement_noise_variance = 0; // Just above the guarded innovation threshold.
    kalman_cv_correct(&filter, 12);
    CHECK(filter.estimate == 12 && filter.p00 == 0);
}

#ifdef TRACK_TEST_HOST
static const struct test_case cases[] = {
    TEST_CASE(test_measurement_wire_domain_matrix),
    TEST_CASE(test_measurement_length_domain_preserves_output),
    TEST_CASE(test_prediction_interval_and_noise_matrix),
    TEST_CASE(test_correction_noise_and_residual_matrix),

    TEST_CASE(test_angle_wrap),
    TEST_CASE(test_angle_wrap_boundaries),
    TEST_CASE(test_angle_wrap_multiple_turns),
    TEST_CASE(test_angle_wrap_large_finite_values),
    TEST_CASE(test_correction_rejects_invalid_innovation_covariance),
    TEST_CASE(test_gate),
    TEST_CASE(test_cv_prediction_and_covariance),
    TEST_CASE(test_cv_correction),
    TEST_CASE(test_angle_prediction_and_correction),
    TEST_CASE(test_stationary_convergence),
    TEST_CASE(test_decode_semicircle),
    TEST_CASE(test_prediction_preserves_lifecycle),
    TEST_CASE(test_elapsed_time_and_wraparound),
    TEST_CASE(test_track_restart),
    TEST_CASE(test_confirmation_coasting_and_loss),
    TEST_CASE(test_tentative_miss_and_inactive_calls),
    TEST_CASE(test_lost_inactive_calls_and_restart),
};

int main(int argc, char **p_argv)
{
    return run_tests(argc, p_argv, cases, sizeof(cases) / sizeof(cases[0]));
}

#endif
