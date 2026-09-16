#ifndef FRAME_HANDLER_H
#define FRAME_HANDLER_H

#include "frame_parser.h"
#include "kalman_cv.h"

struct frame_handler {
    struct kalman_cv_filter range_filter;
    struct kalman_cv_filter azimuth_filter;

    bool is_filter_initialized;
    uint32_t last_measurement_time_ms;
};

void frame_handler_init(struct frame_handler *p_handler);
void frame_handler_reset(struct frame_handler *p_handler);

void handle_frame(
    struct frame_handler *p_handler,
    const struct frame_parser *p_parser);

#endif