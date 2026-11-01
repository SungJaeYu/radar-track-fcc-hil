#ifndef FRAME_HANDLER_H
#define FRAME_HANDLER_H

#include <stdint.h>
#include <stdbool.h>

#include "frame_parser.h"
#include "track.h"

struct frame_handler {
    struct track track;

    // Board uptime (ms) when the last decoded and validated radar measurement was processed.
    int64_t last_valid_rx_ms; 
    // Set by a valid radar measurement; cleared by initialization or timeout reset.
    bool timeout_armed;
};

struct received_frame {
    FrameType type;
    uint8_t payload_length;
    uint8_t payload[MAX_PAYLOAD];
    int64_t received_at_ms;
};

void frame_handler_init(struct frame_handler *p_handler);
void frame_handler_reset(struct frame_handler *p_handler);
void frame_handler_check_timeout(struct frame_handler *p_handler, int64_t now_ms);



void handle_frame(struct frame_handler *p_handler, const struct received_frame *p_frame);

int frame_handler_submit(const struct received_frame *p_frame);


#endif
