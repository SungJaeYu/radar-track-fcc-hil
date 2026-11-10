#ifndef KALMAN_CV_H
#define KALMAN_CV_H

// Constant-velocity state [value, rate] with covariance matrix [[p00, p01], [p10, p11]].
// Range mode uses m and m/s; angle mode uses rad and rad/s.
// process_noise is acceleration variance; measurement_noise is value variance.
// Covariance units follow the corresponding state products.
struct kalman_cv_filter {
    float estimate;
    float rate;

    float p00;
    float p01;
    float p10;
    float p11;

    float process_noise;
    float measurement_noise;
};

// Initialize a non-NULL filter with caller-selected state, covariance, and noise.
// Call before predict/correct. Inputs are not validated; the caller owns the filter
// and supplies finite values and physically valid covariance/noise parameters.
void kalman_cv_init(
    struct kalman_cv_filter *p_kf,
    float initial_value,
    float initial_value_rate,
    float p00, float p01,
    float p10, float p11,
    float process_noise,
    float measurement_noise
);

// Predict a non-NULL initialized linear filter forward by dt_s seconds.
// The caller validates the time interval before calling.
void kalman_cv_predict(
    struct kalman_cv_filter *p_kf,
    float dt_s);

// Predict an initialized angular filter using dt_s seconds; wrap the estimate to (-pi, pi].
void kalman_cv_predict_angle(
    struct kalman_cv_filter *p_kf,
    float dt_s);

// Correct an initialized linear filter with a measurement in the state value unit.
// Skip correction if innovation covariance is <= 1e-6; no status is returned.
void kalman_cv_correct(
    struct kalman_cv_filter *p_kf,
    float value_measurement);

// Correct an initialized angular filter with a measurement in radians.
// Wrap the residual and updated estimate; the same covariance guard applies.
void kalman_cv_correct_angle(
    struct kalman_cv_filter *p_kf,
    float angle_measurement_rad);

#endif
