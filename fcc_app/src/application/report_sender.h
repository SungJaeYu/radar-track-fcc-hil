#ifndef REPORT_SENDER_H
#define REPORT_SENDER_H

#include <stdbool.h>
#include <stdint.h>

#include "protocol/track_message.h"
#include "tracking/track.h"

// Owned by the handler thread. Zero-initialize once, not on INIT/RESET/timeout.
struct report_sender {
    // Consume a sequence for every report attempt, including encode/TX failures.
    uint32_t next_track_report_sequence;
};

// Read independently atomic RX counters, encode STATUS, and submit a copied frame.
// Include the handler-owned stale measurement count without resetting it.
// Call from the handler thread. Log failures without retrying or resetting counters.
// Submission success means queued, not delivered to the peer.
void report_sender_submit_status(uint32_t stale_measurements);

// Require non-NULL sender/track and exclusive handler-thread ownership.
// With input_present, time identifies the validated measurement even if tracking rejected it.
// Without input, encode a zero input timestamp for a reset event.
// Report invalid estimates as zero; preserve the track and consume one sequence.
// Log encode/TX failures without retrying. No pointers are retained after return.
void report_sender_submit_track(struct report_sender *p_sender, const struct track *p_track,
                                uint32_t input_sensor_ms, bool input_present, TrackReportResult result);

#endif
