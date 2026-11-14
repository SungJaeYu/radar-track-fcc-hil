#include <assert.h>
#include <math.h>

#include "../../common/test_runner.h"
#include "tracking/gating.h"

// Independent test conversion, intentionally not imported from production constants.
#define TEST_PI_F 3.14159265358979323846f
#define GATE_M ((float)CONFIG_RADAR_RANGE_GATE_M)
#define GATE_RAD ((float)CONFIG_RADAR_AZIMUTH_GATE_DEG * TEST_PI_F / 180.0f)

static void range_boundary(void)
{
    // A positive reference keeps both signed errors within physically positive ranges.
    const float predicted = GATE_M + 100.0f;
    assert(gating_check(predicted + GATE_M, predicted, 0.0f, 0.0f));
    assert(gating_check(predicted - GATE_M, predicted, 0.0f, 0.0f));
    assert(gating_check(predicted + GATE_M - 0.5f, predicted, 0.0f, 0.0f));
    assert(gating_check(predicted - GATE_M + 0.5f, predicted, 0.0f, 0.0f));
    assert(!gating_check(nextafterf(predicted + GATE_M, INFINITY), predicted, 0.0f, 0.0f));
    assert(!gating_check(predicted - GATE_M - 0.5f, predicted, 0.0f, 0.0f));
}

static void angle_boundary(void)
{
    assert(gating_check(1000.0f, 1000.0f, GATE_RAD, 0.0f));
    assert(gating_check(1000.0f, 1000.0f, -GATE_RAD, 0.0f));
    assert(gating_check(1000.0f, 1000.0f, nextafterf(GATE_RAD, 0.0f), 0.0f));
    assert(gating_check(1000.0f, 1000.0f, nextafterf(-GATE_RAD, 0.0f), 0.0f));
#if CONFIG_RADAR_AZIMUTH_GATE_DEG < 180
    assert(!gating_check(1000.0f, 1000.0f, nextafterf(GATE_RAD, INFINITY), 0.0f));
    assert(!gating_check(1000.0f, 1000.0f, nextafterf(-GATE_RAD, -INFINITY), 0.0f));
#else
    // At 180 degrees, even an angle just beyond pi wraps back inside the gate.
    assert(gating_check(1000.0f, 1000.0f, nextafterf(TEST_PI_F, INFINITY), 0.0f));
#endif
}

static void both_required(void)
{
    const float predicted = GATE_M + 100.0f;
    assert(gating_check(predicted + GATE_M * 0.5f, predicted, GATE_RAD * 0.5f, 0.0f));
    assert(!gating_check(predicted + GATE_M + 1.0f, predicted, 0.0f, 0.0f));
#if CONFIG_RADAR_AZIMUTH_GATE_DEG < 180
    const float outside_angle = GATE_RAD + 0.01f;
    assert(!gating_check(predicted, predicted, outside_angle, 0.0f));
    assert(!gating_check(predicted + GATE_M + 1.0f, predicted, outside_angle, 0.0f));
#endif
}

static void wrapped_error(void)
{
    // Each input is near the opposite pi endpoint; their shortest separation is half a gate.
    const float offset = GATE_RAD * 0.25f;
    assert(gating_check(1000.0f, 1000.0f, -TEST_PI_F + offset, TEST_PI_F - offset));
    assert(gating_check(1000.0f, 1000.0f, TEST_PI_F - offset, -TEST_PI_F + offset));
    assert(gating_check(1000.0f, 1000.0f, -TEST_PI_F, TEST_PI_F));
#if CONFIG_RADAR_AZIMUTH_GATE_DEG < 180
    const float outside_offset = GATE_RAD * 0.75f;
    assert(!gating_check(1000.0f, 1000.0f, -TEST_PI_F + outside_offset, TEST_PI_F - outside_offset));
    assert(!gating_check(1000.0f, 1000.0f, TEST_PI_F - outside_offset, -TEST_PI_F + outside_offset));
#endif
}

static const struct test_case cases[] = {
    TEST_CASE(range_boundary),
    TEST_CASE(angle_boundary),
    TEST_CASE(both_required),
    TEST_CASE(wrapped_error),
};

int main(int argc, char **p_argv)
{
    return run_tests(argc, p_argv, cases, sizeof(cases) / sizeof(cases[0]));
}
