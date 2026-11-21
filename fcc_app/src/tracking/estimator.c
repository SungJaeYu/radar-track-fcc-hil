#include "tracking/estimator.h"


void estimator_init(struct estimator *p_estimator, float initial_range_m, float initial_azimuth_rad)
{
    // Range tuning defaults: p00/R = 25 m^2, p11 = 10000 (m/s)^2,
    // q = 100 (m/s^2)^2; these are initial tuning values, not sensor calibration data.
    kalman_cv_init(
        &p_estimator->range_filter,
        initial_range_m,
        0.0f,      // initial_range_rate
        25.0f,     // p00: range uncertainty
        0.0f,      // p01
        0.0f,      // p10
        10000.0f,  // p11: range-rate uncertainty
        100.0f,    // q: acceleration noise variance
        25.0f      // R: measurement noise variance
    );

    kalman_cv_init(
        &p_estimator->azimuth_filter,
        initial_azimuth_rad,
        0.0f,      // initial_azimuth_rate
        0.00121847f,     // p00: azimuth uncertainty: std dev = 2 deg, p00 = (2 * PI / 180)^2 [rad^2]
        0.0f,      // p01
        0.0f,      // p10
        0.03046174f,  // p11: angular velocity uncertainty: std dev = 10 deg/s, p11 = (10 * PI / 180)^2 [(rad/s)^2]
        0.00761544f,    // q: angular acceleration noise variance: std dev = 5 deg/s^2, q = (5 * PI / 180)^2 [(rad/s^2)^2]
        0.00121847f      // R: measurement noise variance: std dev = 2 deg, R = (2 * PI / 180)^2 [rad^2]
    );
}

void estimator_predict(struct estimator *p_estimator, float interval_s)
{
    kalman_cv_predict(&p_estimator->range_filter, interval_s);
    kalman_cv_predict_angle(&p_estimator->azimuth_filter, interval_s);
}

void estimator_correct(struct estimator *p_estimator, float measured_range_m, float measured_azimuth_rad)
{
    kalman_cv_correct(&p_estimator->range_filter, measured_range_m);
    kalman_cv_correct_angle(&p_estimator->azimuth_filter, measured_azimuth_rad);
}

struct estimator_output estimator_get_output(const struct estimator *p_estimator)
{
    const struct estimator_output output = {
        .range_m = p_estimator->range_filter.estimate,
        .range_rate_mps = p_estimator->range_filter.rate,
        .azimuth_rad = p_estimator->azimuth_filter.estimate,
        .azimuth_rate_radps = p_estimator->azimuth_filter.rate,
    };

    return output;
}
