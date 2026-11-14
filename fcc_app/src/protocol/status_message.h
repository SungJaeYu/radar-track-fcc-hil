#ifndef STATUS_MESSAGE_H
#define STATUS_MESSAGE_H

#include <stdint.h>
#include "protocol/protocol_frame.h"

// Transport-independent values copied from the RX diagnostic owners.
struct status_report {
    uint32_t crc_errors;
    uint32_t length_errors;
    uint32_t frame_timeouts;
    uint32_t queue_drops;
    uint32_t ring_overflow_events;
    uint32_t rx_dropped_bytes;
    uint32_t recovery_discarded_bytes;
};

#define STATUS_REPORT_PAYLOAD_LENGTH 28U

// Encode seven big-endian uint32 counters in the declaration order above.
// Set STATUS_REPORT type and the 28-byte payload length; no source counters are reset.
// Return 0 on success or -EINVAL for NULL input/output.
// Leave the output unchanged on failure.
int status_message_encode(const struct status_report *p_stats, struct protocol_frame *out_frame);

#endif
