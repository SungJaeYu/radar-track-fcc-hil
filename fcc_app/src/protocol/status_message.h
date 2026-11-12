#ifndef STATUS_MESSAGE_H
#define STATUS_MESSAGE_H

#include "transport/rx_frame_processor.h"
#include "protocol/protocol_frame.h"

// Build a STATUS_REPORT with four big-endian uint32 counters:
// CRC errors, length errors, frame timeouts, and queue drops.
// Set the frame type and the 16-byte payload length.
// Return 0 on success or -EINVAL for NULL input/output.
// Leave the output unchanged on failure.
int status_message_encode(const struct rx_frame_stats *p_stats, struct protocol_frame *out_frame);

#endif
