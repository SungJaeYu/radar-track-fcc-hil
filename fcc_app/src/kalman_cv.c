#include "kalman_cv.h"

#include <stdbool.h>
#include "angle_utils.h"

static void kalman_cv_predict_internal(struct kalman_cv_filter *p_kf, float dt, bool is_angle);
static void kalman_cv_correct_internal(struct kalman_cv_filter *p_kf, float value_measurement, bool is_angle);

void kalman_cv_init(struct kalman_cv_filter *p_kf, float initial_value, float initial_value_rate, float p00, float p01, float p10, float p11, float process_noise, float measurement_noise)
{
    p_kf->estimate = initial_value;
    p_kf->rate = initial_value_rate;
    p_kf->p00 = p00;
    p_kf->p01 = p01;
    p_kf->p10 = p10;
    p_kf->p11 = p11;

    p_kf->process_noise = process_noise;
    p_kf->measurement_noise = measurement_noise;
}

void kalman_cv_predict(struct kalman_cv_filter *p_kf, float dt)
{
    kalman_cv_predict_internal(p_kf, dt, false);
}

void kalman_cv_predict_angle(struct kalman_cv_filter *p_kf, float dt)
{
    kalman_cv_predict_internal(p_kf, dt, true);
}

void kalman_cv_correct(struct kalman_cv_filter *p_kf, float value_measurement)
{
    kalman_cv_correct_internal(p_kf, value_measurement, false);
}

void kalman_cv_correct_angle(struct kalman_cv_filter *p_kf, float angle_measurement)
{
    kalman_cv_correct_internal(p_kf, angle_measurement, true);
}

static void kalman_cv_predict_internal(struct kalman_cv_filter *p_kf, float dt, bool is_angle)
{
    // State Prediction
    p_kf->estimate += p_kf->rate * dt;
    if (is_angle) {
        p_kf->estimate = wrap_angle_deg(p_kf->estimate);
    }

    // Covariance Prediction
    // P' = F P F(T) + Q
    const float old_p00 = p_kf->p00;
    const float old_p01 = p_kf->p01;
    const float old_p10 = p_kf->p10;
    const float old_p11 = p_kf->p11;
    const float q = p_kf->process_noise;

    p_kf->p00 = 
        old_p00 + 
        dt * old_p10 + 
        dt * old_p01 + 
        dt * dt * old_p11 +
        q * (dt * dt * dt* dt) / 4.0f;
    p_kf->p01 = 
        old_p01 + 
        dt * old_p11 +
        q * (dt * dt * dt) / 2.0f;
    p_kf->p10 = 
        old_p10 + 
        dt * old_p11 +
        q * (dt * dt * dt) / 2.0f;
    p_kf->p11 =
        old_p11 +
        q * (dt * dt);
}

static void kalman_cv_correct_internal(struct kalman_cv_filter *p_kf, float value_measurement, bool is_angle)
{
    // Residual
    float residual = value_measurement - p_kf->estimate;
    
    if (is_angle) {
        residual = wrap_angle_deg(residual);
    }

    // Innovation covariance
    const float s = p_kf->p00 + p_kf->measurement_noise;

    if (s <= 1e-6f) {
        return;
    }

    // Kalman Gain
    const float k0 = p_kf->p00 / s;
    const float k1 = p_kf->p10 / s;

    // State Correction
    p_kf->estimate += k0 * residual;
    if (is_angle) {
        p_kf->estimate = wrap_angle_deg(p_kf->estimate);
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
