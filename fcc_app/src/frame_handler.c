#include <zephyr/sys/printk.h>
#include <zephyr/kernel.h>

#include "frame_handler.h"
#include "radar_message.h"
#include "gating.h"

// Expire the track after 1000 ms without a decoded and validated radar measurement.
#define RADAR_RX_TIMEOUT_MS 1000

void frame_handler_init(struct frame_handler *p_handler) 
{
    track_init(&p_handler->track);

    // Arm timeout monitoring only after the next valid radar measurement.
    p_handler->last_valid_rx_ms = 0;
    p_handler->timeout_armed = false;
}

void frame_handler_reset(struct frame_handler *p_handler) 
{
    frame_handler_init(p_handler);
}

void frame_handler_check_timeout(struct frame_handler *p_handler, int64_t now_ms)
{
    // No timeout before the first valid measurement after initialization or reset.
    if (!p_handler->timeout_armed) {
        return;
    }

    // Both times use board uptime in ms, not the sender's measurement timestamp.
    const int64_t elapsed_ms = now_ms - p_handler->last_valid_rx_ms;

    if (elapsed_ms < RADAR_RX_TIMEOUT_MS) {
        return;
    }

    // Clear the stale track and disarm monitoring so this timeout is logged once.
    frame_handler_reset(p_handler);
    printk("Radar measurement timeout: track cleared\n");
}

void handle_frame(struct frame_handler *p_handler, const struct frame_parser *p_parser)
{
    switch (p_parser->type) {
    case TYPE_INIT:
        printk("TYPE_INIT\n");
        frame_handler_init(p_handler);
        break;

    case TYPE_RADAR_MEAS: {
        struct radar_measurement rm;

        if (!decode_radar_measurement(
                p_parser->payload,
                p_parser->payload_length,
                &rm)) {
            printk("Invalid radar payload\n");
            break;
        }

        if (!validate_radar_measurement(&rm)) {
            printk("Invalid radar measurement\n");
            break;
        }
    
        // Record processing time for valid radar payloads, before timestamp/gate checks.
        // A later tracking rejection still counts as receiving a valid measurement payload.
        p_handler->last_valid_rx_ms = k_uptime_get();
        p_handler->timeout_armed = true;

        printk("range: %.2f\n", rm.range_m);
        printk("azimuth: %.2f\n", rm.azimuth_rad);

        if (p_handler->track.state == TRACK_EMPTY) {            
            track_start(&p_handler->track, (float)rm.range_m, (float)rm.azimuth_rad, rm.timestamp_ms);
            
            printk("Filters initialized at range: %.2f\n", rm.range_m);
            printk("Filters initialized at azimuth: %.2f\n", rm.azimuth_rad);
        } else {

            if(!track_predict(&p_handler->track, rm.timestamp_ms)) {
                printk("Invalid measurement interval\n");
                break;
            }

            const float predicted_range = p_handler->track.range_filter.estimate;
            const float predicted_azimuth = p_handler->track.azimuth_filter.estimate;
            
            printk("predicted range: %.2f\n", (double)predicted_range);
            printk("predicted azimuth: %.2f\n", (double)predicted_azimuth);

            // Gating
            const bool is_in_gate = gating_check(
                rm.range_m,
                predicted_range,
                rm.azimuth_rad,
                predicted_azimuth
            );

            // Correction
            if (is_in_gate) {
                track_hit(&p_handler->track, rm.range_m, rm.azimuth_rad);
                printk("Measurement accepted\n");
            } else {
                track_miss(&p_handler->track);
                printk("Measurement rejected by gate\n");
            }

            const float filtered_range =
                p_handler->track.range_filter.estimate;

            const float filtered_azimuth =
                p_handler->track.azimuth_filter.estimate;

  
            printk("filtered range: %.2f\n",
                (double)filtered_range);

            printk("filtered azimuth: %.2f\n",
                (double)filtered_azimuth);

            printk("estimated range rate: %.2f\n",
                (double)p_handler->track.range_filter.rate);
            printk("estimated azimuth rate: %.2f\n",
                (double)p_handler->track.azimuth_filter.rate);
        }
        break;
    }
    case TYPE_STATUS:
        printk("TYPE_STATUS\n");
        break;

    case TYPE_COMMAND:
        printk("TYPE_COMMAND\n");
        break;

    case TYPE_RESET:
        printk("TYPE RESET\n");
        frame_handler_reset(p_handler);
        break;
    default:
        printk("Unknown frame type: 0x%02X\n", p_parser->type);
        break;
    }
}
