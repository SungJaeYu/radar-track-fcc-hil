#ifndef UART_TX_H
#define UART_TX_H

#include <stdbool.h>

#include "protocol_frame.h"

struct send_frame {
    struct protocol_frame frame;
};

int uart_tx_init(void);

int uart_tx_submit(const struct send_frame *p_frame);



#endif