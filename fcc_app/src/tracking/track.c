#include "tracking/track.h"

#include <zephyr/sys/__assert.h>

#if defined(CONFIG_RADAR_PERF_MONITOR) && defined(CONFIG_TIMING_FUNCTIONS)
#include <zephyr/timing/timing.h>
#include "diagnostics/perf_monitor.h"
#endif

#include "tracking/estimator.h"

// Lifecycle thresholds are independent of the configurable measurement gates.
#define TRACK_CONFIRM_HITS 3U
#define TRACK_LOST_MISSES 5U

void track_init(struct track *p_track)
{
    p_track->state = TRACK_STATE_EMPTY;
    p_track->hit_count = 0U;
    p_track->miss_count = 0U;
    p_track->estimate_sensor_ms = 0U;
}

void track_start(struct track *p_track, float initial_range_m, float initial_azimuth_rad, uint32_t sensor_time_ms)
{
    estimator_init(&p_track->estimator, initial_range_m, initial_azimuth_rad);

    p_track->state = TRACK_STATE_TENTATIVE;
    p_track->hit_count = 1U;
    p_track->miss_count = 0U;

    p_track->estimate_sensor_ms = sensor_time_ms;
}

struct track track_predict_next(const struct track *p_current, uint32_t target_sensor_ms)
{
    __ASSERT(p_current->state == TRACK_STATE_TENTATIVE ||
             p_current->state == TRACK_STATE_CONFIRMED ||
             p_current->state == TRACK_STATE_COASTING,
             "Prediction requires an active track");

    const uint32_t prediction_interval_ms = target_sensor_ms - p_current->estimate_sensor_ms;
    __ASSERT(prediction_interval_ms > 0U && prediction_interval_ms < TRACK_SENSOR_HALF_RANGE_MS,
             "Prediction requires a validated forward timestamp");

    struct track next = *p_current;

    const float prediction_interval_s = (float)prediction_interval_ms / 1000.0f;
#if defined(CONFIG_RADAR_PERF_MONITOR) && defined(CONFIG_TIMING_FUNCTIONS)
    timing_t predict_begin = timing_counter_get();
#endif
    estimator_predict(&next.estimator, prediction_interval_s);
#if defined(CONFIG_RADAR_PERF_MONITOR) && defined(CONFIG_TIMING_FUNCTIONS)
    timing_t predict_end = timing_counter_get();
    const uint64_t cycles = timing_cycles_get(&predict_begin, &predict_end);
    perf_monitor_record_predict(cycles);
#endif
    next.estimate_sensor_ms = target_sensor_ms;
    return next;
}

struct estimator_output track_get_estimate(const struct track *p_track)
{
    return estimator_get_output(&p_track->estimator);
}

void track_apply_measurement(struct track *p_track, float measured_range_m, float measured_azimuth_rad)
{
    switch (p_track->state) {
    case TRACK_STATE_EMPTY:
    case TRACK_STATE_LOST:
        return;

    case TRACK_STATE_TENTATIVE:
    case TRACK_STATE_CONFIRMED:
    case TRACK_STATE_COASTING: {
#if defined(CONFIG_RADAR_PERF_MONITOR) && defined(CONFIG_TIMING_FUNCTIONS)
        timing_t correct_begin = timing_counter_get();
#endif
        estimator_correct(&p_track->estimator, measured_range_m, measured_azimuth_rad);
#if defined(CONFIG_RADAR_PERF_MONITOR) && defined(CONFIG_TIMING_FUNCTIONS)
        timing_t correct_end = timing_counter_get();
        const uint64_t cycles = timing_cycles_get(&correct_begin, &correct_end);
        perf_monitor_record_correct(cycles);
#endif

        if (p_track->hit_count < TRACK_CONFIRM_HITS) {
            p_track->hit_count++;
        }
        p_track->miss_count = 0U;

        if (p_track->state == TRACK_STATE_COASTING ||
            (p_track->state == TRACK_STATE_TENTATIVE && p_track->hit_count >= TRACK_CONFIRM_HITS)) {
            p_track->state = TRACK_STATE_CONFIRMED;
        }
        return;
    }

    default:
        __ASSERT(false, "Invalid track state: %d", (int)p_track->state);
        // Clear invalid state when assertions are disabled.
        track_init(p_track);
        return;
    }
}

void track_apply_miss(struct track *p_track)
{
    switch (p_track->state) {
    case TRACK_STATE_EMPTY:
    case TRACK_STATE_LOST:
        return;

    case TRACK_STATE_TENTATIVE:
        track_init(p_track);
        return;

    case TRACK_STATE_CONFIRMED:
    case TRACK_STATE_COASTING: {
        p_track->hit_count = 0U;
        if (p_track->miss_count < TRACK_LOST_MISSES) {
            p_track->miss_count++;
        }

        if (p_track->miss_count >= TRACK_LOST_MISSES) {
            p_track->state = TRACK_STATE_LOST;
        } else {
            p_track->state = TRACK_STATE_COASTING;
        }
        return;
    }

    default:
        __ASSERT(false, "Invalid track state: %d", (int)p_track->state);
        // Clear invalid state when assertions are disabled.
        track_init(p_track);
        return;
    }
}
