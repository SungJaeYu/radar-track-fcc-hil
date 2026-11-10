#ifndef KALMAN_CV_H
#define KALMAN_CV_H

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

void kalman_cv_init(
    struct kalman_cv_filter *p_kf,
    float initial_value,
    float initial_value_rate,
    float p00, float p01, 
    float p10, float p11,
    float process_noise,
    float measurement_noise
);

void kalman_cv_predict(
    struct kalman_cv_filter *p_kf,
    float dt);

void kalman_cv_predict_angle(
    struct kalman_cv_filter *p_kf,
    float dt);

void kalman_cv_correct(
    struct kalman_cv_filter *p_kf,
    float value_measurement);

void kalman_cv_correct_angle(
    struct kalman_cv_filter *p_kf,
    float angle_measurement);

#endif