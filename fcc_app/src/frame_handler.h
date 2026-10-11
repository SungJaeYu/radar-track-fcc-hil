#ifndef FRAME_HANDLER_H
#define FRAME_HANDLER_H

#include "frame_parser.h"
#include "track.h"

struct frame_handler {
    struct track track;
};

void frame_handler_init(struct frame_handler *p_handler);
void frame_handler_reset(struct frame_handler *p_handler);

void handle_frame(
    struct frame_handler *p_handler,
    const struct frame_parser *p_parser);

#endif