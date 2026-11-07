#ifndef UART_TX_H
#define UART_TX_H

#include <stdbool.h>

#include "frame_parser.h"

struct send_frame {
    FrameType type;
    uint8_t payload_length;
    uint8_t payload[MAX_PAYLOAD];
};

int uart_tx_init(void);

int uart_tx_submit(const struct send_frame *p_frame);



#endif