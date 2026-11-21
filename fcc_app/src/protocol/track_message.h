#ifndef TRACK_MESSAGE_H
#define TRACK_MESSAGE_H

#include <stdint.h>

#include "protocol/protocol_frame.h"

#define TRACK_REPORT_PAYLOAD_LENGTH 32
#define TRACK_REPORT_FLAG_INPUT_PRESENT  (1U << 0)
#define TRACK_REPORT_FLAG_ESTIMATE_VALID (1U << 1)

struct track_report {
    uint32_t report_sequence;
    // Sensor clock; wire offsets 4 and 8 match the PC timestamp fields.
    uint32_t input_sensor_ms;
    uint32_t estimate_sensor_ms;
    float range_m;
    float azimuth_rad;
    float range_rate_mps;
    float azimuth_rate_radps;
    uint8_t state;
    uint8_t result;
    uint8_t flags;
    uint8_t reserved;
};

typedef enum {
    TRACK_REPORT_RESULT_STARTED       = 0,
    TRACK_REPORT_RESULT_UPDATED       = 1,
    TRACK_REPORT_RESULT_GATE_REJECTED = 2,
    TRACK_REPORT_RESULT_TIME_REJECTED = 3,
    TRACK_REPORT_RESULT_RESET         = 4,
} TrackReportResult;


// Encode track report data in the declaration order above.
// Set TRACK_REPORT type and the 32-byte payload length; the source report is unchanged.
// Return 0 on success or -EINVAL for NULL input/output.
// Leave the output unchanged on failure.
int track_message_encode(const struct track_report *p_report, struct protocol_frame *out_frame);


#endif
