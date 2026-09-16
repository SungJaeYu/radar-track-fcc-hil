#include "kalman_scalar.h"

void kalman_scalar_init(
    struct kalman_scalar_filter *p_kf,
    float initial_estimate,
    float initial_error_covariance,
    float process_noise,
    float measurement_noise) {
    p_kf->estimate = initial_estimate;
    p_kf->error_covariance = initial_error_covariance;
    p_kf->process_noise = process_noise;
    p_kf->measurement_noise = measurement_noise;
}

float kalman_scalar_update(
    struct kalman_scalar_filter *p_kf,
    float measurement) {
    float kalman_gain;

    // Predict covariance
    p_kf->error_covariance += p_kf->process_noise;

    // Calculate Kalman gain
    kalman_gain = p_kf->error_covariance / (p_kf->error_covariance + p_kf->measurement_noise);

    // Update estimate
    p_kf->estimate += kalman_gain * (measurement - p_kf->estimate);
    
    // Update covariance
    p_kf->error_covariance = (1.0f - kalman_gain) * p_kf->error_covariance;
    
    // return estimate
    return p_kf->estimate;
}