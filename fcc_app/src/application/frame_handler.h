#ifndef FRAME_HANDLER_H
#define FRAME_HANDLER_H

#include <stdint.h>
#include <stdbool.h>

#include "application/radar_processor.h"
#include "application/report_sender.h"
#include "protocol/protocol_frame.h"

// Processing-thread-owned state; access through the frame queue in normal operation.
struct frame_handler {
    struct radar_processor radar;
    // Report state is preserved across INIT, RESET, and measurement timeout.
    struct report_sender report_sender;
};

struct received_frame {
    struct protocol_frame frame;
    // Board uptime at RX parser completion/submission, not ISR arrival or handler execution.
    int64_t received_at_ms;
};

// Require a non-NULL handler and exclusive access to its state.
// Zero-initialize before first use; initialization/reset preserve report state.
// Reset tracking and disarm timeout until the next valid measurement payload.
// Does not purge queued frames or clear cumulative RX diagnostic counters.
void frame_handler_init(struct frame_handler *p_handler);
// Same state reset as frame_handler_init(); pending queue contents are preserved.
void frame_handler_reset(struct frame_handler *p_handler);
// Check a non-NULL handler using board uptime in ms.
// After CONFIG_RADAR_MEASUREMENT_TIMEOUT_MS (default 1000 ms), reset and disarm once.
// Monitoring is inactive before the first valid measurement after init/reset.
void frame_handler_check_timeout(struct frame_handler *p_handler, int64_t now_ms);

// Process a non-NULL, assembled frame against a non-NULL handler in its owner context.
// The RX path has already checked length and CRC; measurement decoding/gating happen here.
// STATUS_REQUEST queues a STATUS_REPORT; COMMAND is not implemented.
void handle_frame(struct frame_handler *p_handler, const struct received_frame *p_frame);

// Copy a received frame and its timestamp into the eight-entry queue without waiting.
// Return 0 when queued, -EINVAL for NULL or invalid payload length, or -ENOMSG if full.
// Success means queued, not processed; the caller may reuse its object on return.
int frame_handler_submit(const struct received_frame *p_frame);

#endif
