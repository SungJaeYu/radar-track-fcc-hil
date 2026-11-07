#ifndef UART_TX_H
#define UART_TX_H

#include <stdbool.h>

#include "frame_parser.h"

int uart_tx_init(void);

bool uart_tx_submit(void);

struct send_frame {
    FrameType type;
    uint8_t payload_length;
    uint8_t payload[MAX_PAYLOAD];
    int64_t send_at_ms;
};

#endif