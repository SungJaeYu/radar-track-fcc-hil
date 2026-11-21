#ifndef RADAR_PROCESSOR_H
#define RADAR_PROCESSOR_H

#include <stdbool.h>
#include <stdint.h>

#include "protocol/protocol_frame.h"
#include "protocol/track_message.h"
#include "tracking/track.h"

// Single-owner state; all operations run in the existing handler thread.
struct radar_processor {
    struct track track;
    // Board uptime at RX frame completion, not the sender's measurement time.
    int64_t last_valid_rx_uptime_ms;
    // Armed by a valid measurement payload, including later tracking rejections.
    bool timeout_armed;
};

// Successful processing identifies an input; the track may still be invalid.
struct radar_process_result {
    uint32_t input_sensor_ms;
    TrackReportResult result;
};

// Require a non-NULL, exclusively owned processor. Call before first processing.
// Clear the track and disarm timeout; filter storage remains until track startup.
void radar_processor_reset(struct radar_processor *p_processor);

// Require non-NULL pointers and exclusive processor ownership. No pointers are retained.
// The caller has selected MEAS_REPORT and validated the outer frame length/CRC.
// Decode and validate the payload, then update tracking and measurement monitoring.
// Return false on invalid payload without modifying processor or output result.
// Return true for valid payloads, including gate/time rejection; fill the result.
// Does not submit reports, consume report sequence numbers, or access UART/queues.
bool radar_processor_process_measurement(struct radar_processor *p_processor,
                                         const struct protocol_frame *p_frame, int64_t received_uptime_ms,
                                         struct radar_process_result *out_result);

// Require a non-NULL processor in its owner context; use board uptime in ms.
// Return whether armed monitoring has expired, without changing state or logging.
// Remain true until the owner resets the processor or receives a valid measurement.
bool radar_processor_is_timed_out(const struct radar_processor *p_processor, int64_t now_uptime_ms);

#endif
