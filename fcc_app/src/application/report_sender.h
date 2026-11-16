#ifndef REPORT_SENDER_H
#define REPORT_SENDER_H

#include <stdint.h>

#include "protocol/track_message.h"
#include "tracking/track.h"

// Owned by the handler thread. Zero-initialize once, not on INIT/RESET/timeout.
struct report_sender {
    // Consume a sequence for every report attempt, including encode/TX failures.
    uint32_t report_sequence;
};

// Read independently atomic RX counters, encode STATUS, and submit a copied frame.
// Call from the handler thread. Log failures without retrying or resetting counters.
// Submission success means queued, not delivered to the peer.
void report_sender_submit_status(void);

// Require non-NULL sender/track and exclusive handler-thread ownership.
// Input time identifies the validated measurement, even if tracking rejected it.
// Report invalid estimates as zero; preserve the track and consume one sequence.
// Log encode/TX failures without retrying. No pointers are retained after return.
void report_sender_submit_track(struct report_sender *p_sender, const struct track *p_track,
                                uint32_t input_timestamp_ms, TrackReportResult result);

#endif
