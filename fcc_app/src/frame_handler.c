#include <zephyr/sys/printk.h>
#include <zephyr/kernel.h>

#include "frame_handler.h"
#include "radar_message.h"
#include "gating.h"

void frame_handler_init(struct frame_handler *p_handler) 
{
    p_handler->is_filter_initialized = false;
    p_handler->last_measurement_time_ms = 0;
}

void frame_handler_reset(struct frame_handler *p_handler) 
{
    frame_handler_init(p_handler);
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
    
        if (!p_handler->is_filter_initialized) {            
            kalman_cv_init(
                &p_handler->range_filter,
                (float)rm.range,      // initial_range
                0.0f,      // initial_range_rate
                25.0f,     // p00: range uncertainty
                0.0f,      // p01
                0.0f,      // p10
                10000.0f,  // p11: range-rate uncertainty
                100.0f,    // q: acceleration noise variance
                25.0f      // R: measurement noise variance
            );

            kalman_cv_init(
                &p_handler->azimuth_filter,
                (float)rm.azimuth,      // initial_azimuth
                0.0f,      // initial_azimuth_rate
                4.0f,     // p00: azimuth uncertainty
                0.0f,      // p01
                0.0f,      // p10
                100.0f,  // p11: range-rate uncertainty
                25.0f,    // q: acceleration noise variance
                4.0f      // R: measurement noise variance
            );

            p_handler->is_filter_initialized = true;
            printk("range: %u\n", rm.range);
            printk("azimuth: %d\n", rm.azimuth);

            printk("Filters initialized at range: %u\n", rm.range);

        } else {
            const uint32_t delta_ms = rm.timestamp_ms - p_handler->last_measurement_time_ms;

            const float dt = (float)delta_ms / 1000.0f;
            
            if (delta_ms == 0U || dt > 1.0f) {
                printk("Invalid measurement dt: %.3f\n", (double)dt);
                p_handler->last_measurement_time_ms = rm.timestamp_ms;
                break;
            }

            printk("range: %u\n", rm.range);
            printk("azimuth: %d\n", rm.azimuth);

            // Prediction
            kalman_cv_predict(
                &p_handler->range_filter, dt);

            kalman_cv_predict_angle(
                &p_handler->azimuth_filter,dt);


            const float predicted_range = p_handler->range_filter.estimate;
            const float predicted_azimuth = p_handler->azimuth_filter.estimate;
            
            printk("predicted range: %.2f\n", (double)predicted_range);
            printk("predicted azimuth: %.2f\n", (double)predicted_azimuth);

            // Gating
            const bool is_in_gate = gating_check(
                (float)rm.range,
                predicted_range,
                (float)rm.azimuth,
                predicted_azimuth
            );

            // Correction
            if (is_in_gate) {
                kalman_cv_correct(&p_handler->range_filter, (float)rm.range);
                kalman_cv_correct_angle(&p_handler->azimuth_filter, (float)rm.azimuth);

                printk("Measurement accepted\n");
            } else {
                printk("Measurement rejected by gate\n");
            }

            const float filtered_range =
                p_handler->range_filter.estimate;

            const float filtered_azimuth =
                p_handler->azimuth_filter.estimate;

  
            printk("filtered range: %.2f\n",
                (double)filtered_range);

            printk("filtered azimuth: %.2f\n",
                (double)filtered_azimuth);

            printk("estimated range rate: %.2f\n",
                (double)p_handler->range_filter.rate);
            printk("estimated azimuth rate: %.2f\n",
                (double)p_handler->azimuth_filter.rate);
        }

        p_handler->last_measurement_time_ms = rm.timestamp_ms;
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