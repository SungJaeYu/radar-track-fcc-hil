#ifndef ESTIMATOR_POLAR_CV_H
#define ESTIMATOR_POLAR_CV_H

#include "tracking/kalman_cv.h"

// Private backend storage, included by estimator.h for static allocation only.
struct estimator {
    struct kalman_cv_filter range_filter;
    struct kalman_cv_filter azimuth_filter;
};

#endif
