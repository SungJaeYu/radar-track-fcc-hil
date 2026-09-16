#ifndef KALMAN_SCALAR_H
#define KALMAN_SCALAR_H

struct kalman_scalar_filter {
    float estimate;
    float error_covariance;
    float process_noise;
    float measurement_noise;
};

void kalman_scalar_init(
    struct kalman_scalar_filter *p_kf,
    float initial_estimate,
    float initial_error_covariance,
    float process_noise,
    float measurement_noise);

float kalman_scalar_update(
    struct kalman_scalar_filter *p_kf,
    float measurement);

#endif