#include "tracking/kalman_cv.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>

#include <zephyr/sys/__assert.h>

#include "tracking/angle_utils.h"

static void kalman_cv_predict_internal(struct kalman_cv_filter *p_kf, float dt_s, bool is_angle);
static void kalman_cv_correct_internal(struct kalman_cv_filter *p_kf, float value_measurement, bool is_angle);

void kalman_cv_init(struct kalman_cv_filter *p_kf, float initial_value, float initial_value_rate, float p00, float p01, float p10, float p11, float process_noise, float measurement_noise)
{
    __ASSERT(p_kf != NULL, "Kalman filter pointer is NULL");
    __ASSERT(isfinite(initial_value), "Initial value must be finite");
    __ASSERT(isfinite(initial_value_rate), "Initial value rate must be finite");
    __ASSERT(isfinite(p00) && p00 >= 0.0f, "p00 value must be finite and nonnegative: %f", (double)p00);
    __ASSERT(isfinite(p01), "p01 value must be finite");
    __ASSERT(isfinite(p10), "p10 value must be finite");
    __ASSERT(isfinite(p11) && p11 >= 0.0f, "p11 value must be finite and nonnegative: %f", (double)p11);
    __ASSERT(
        isfinite(process_noise) && process_noise >= 0.0f,
        "Process noise must be finite and nonnegative: %f", (double)process_noise
    );
    __ASSERT(
        isfinite(measurement_noise) && measurement_noise > 0.0f,
        "Measurement noise must be finite and positive: %f", (double)measurement_noise
    );

    p_kf->estimate = initial_value;
    p_kf->rate = initial_value_rate;
    p_kf->p00 = p00;
    p_kf->p01 = p01;
    p_kf->p10 = p10;
    p_kf->p11 = p11;
    p_kf->process_noise = process_noise;
    p_kf->measurement_noise = measurement_noise;
}

void kalman_cv_predict(struct kalman_cv_filter *p_kf, float dt_s)
{
    kalman_cv_predict_internal(p_kf, dt_s, false);
}

void kalman_cv_predict_angle(struct kalman_cv_filter *p_kf, float dt_s)
{
    kalman_cv_predict_internal(p_kf, dt_s, true);
}

void kalman_cv_correct(struct kalman_cv_filter *p_kf, float value_measurement)
{
    kalman_cv_correct_internal(p_kf, value_measurement, false);
}

void kalman_cv_correct_angle(struct kalman_cv_filter *p_kf, float angle_measurement_rad)
{
    kalman_cv_correct_internal(p_kf, angle_measurement_rad, true);
}

static void kalman_cv_predict_internal(struct kalman_cv_filter *p_kf, float dt_s, bool is_angle)
{
    __ASSERT(p_kf != NULL, "Kalman filter pointer is NULL");
    __ASSERT(
        isfinite(dt_s) && dt_s >= 0.0f,
        "Prediction interval must be finite and nonnegative: %f s", (double)dt_s
    );
    __ASSERT(isfinite(p_kf->estimate), "Estimated value must be finite");
    __ASSERT(isfinite(p_kf->rate), "Estimated rate value must be finite");

    // State Prediction
    p_kf->estimate += p_kf->rate * dt_s;
    if (is_angle) {
        p_kf->estimate = wrap_angle_rad(p_kf->estimate);
    }

    // Covariance Prediction
    // Discrete acceleration variance q uses Q = q * [[dt_s^4/4, dt_s^3/2], [dt_s^3/2, dt_s^2]].
    // P' = F P F(T) + Q
    const float old_p00 = p_kf->p00;
    const float old_p01 = p_kf->p01;
    const float old_p10 = p_kf->p10;
    const float old_p11 = p_kf->p11;
    const float q = p_kf->process_noise;

    p_kf->p00 =
        old_p00 +
        dt_s * old_p10 +
        dt_s * old_p01 +
        dt_s * dt_s * old_p11 +
        q * (dt_s * dt_s * dt_s * dt_s) / 4.0f;
    p_kf->p01 =
        old_p01 +
        dt_s * old_p11 +
        q * (dt_s * dt_s * dt_s) / 2.0f;
    p_kf->p10 =
        old_p10 +
        dt_s * old_p11 +
        q * (dt_s * dt_s * dt_s) / 2.0f;
    p_kf->p11 =
        old_p11 +
        q * (dt_s * dt_s);
}

static void kalman_cv_correct_internal(struct kalman_cv_filter *p_kf, float value_measurement, bool is_angle)
{
    __ASSERT(p_kf != NULL, "Kalman filter pointer is NULL");
    __ASSERT(isfinite(p_kf->estimate), "Estimated value must be finite");
    __ASSERT(isfinite(p_kf->rate), "Estimated rate value must be finite");
    __ASSERT(isfinite(value_measurement), "Measurement value must be finite");

    // Residual
    float residual = value_measurement - p_kf->estimate;

    if (is_angle) {
        residual = wrap_angle_rad(residual);
    }

    // Innovation covariance
    const float s = p_kf->p00 + p_kf->measurement_noise;

    // Skip correction for nonfinite or near-zero/nonpositive innovation covariance.
    // Preserve the prior filter state; this does not repair an already invalid covariance.
    if (!isfinite(s) || s <= 1e-6f) {
        return;
    }

    // Kalman Gain
    const float k0 = p_kf->p00 / s;
    const float k1 = p_kf->p10 / s;

    // State Correction
    p_kf->estimate += k0 * residual;
    if (is_angle) {
        p_kf->estimate = wrap_angle_rad(p_kf->estimate);
    }

    p_kf->rate += k1 * residual;

    // Covariance Correction
    const float pred_p00 = p_kf->p00;
    const float pred_p01 = p_kf->p01;
    const float pred_p10 = p_kf->p10;
    const float pred_p11 = p_kf->p11;

    p_kf->p00 = pred_p00 - k0 * pred_p00;
    p_kf->p01 = pred_p01 - k0 * pred_p01;
    p_kf->p10 = pred_p10 - k1 * pred_p00;
    p_kf->p11 = pred_p11 - k1 * pred_p01;
}
