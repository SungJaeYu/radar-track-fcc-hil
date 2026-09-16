#include "frame_parser.h"

static uint8_t calculate_checksum(const struct frame_parser *p_parser);

void parser_init(struct frame_parser *p_parser) {
    p_parser->state = WAIT_STX;
    p_parser->payload_length = 0;
    p_parser->payload_index = 0;
    p_parser->type = 0;
}

bool parser_feed(struct frame_parser *p_parser, uint8_t byte) {
    switch (p_parser->state) {
    case WAIT_STX:
        if (byte == STX) {
            p_parser->state = WAIT_LEN;
        }
        break;
    case WAIT_LEN:
        if (byte == 0 || byte > MAX_PAYLOAD) {
            p_parser->state = WAIT_STX;
        } else {
            p_parser->payload_length = byte;
            p_parser->state = WAIT_TYPE;
        }
        break;
    case WAIT_TYPE:
        p_parser->type = byte;
        p_parser->state = READ_PAYLOAD;
        break;
    case READ_PAYLOAD:
        p_parser->payload[p_parser->payload_index++] = byte;
        if (p_parser->payload_index == p_parser->payload_length) {
            p_parser->state = WAIT_CRC;
        }
        break;
    case WAIT_CRC:
        p_parser->state = WAIT_STX;
        p_parser->payload_index = 0;

        if (calculate_checksum(p_parser) == byte) {
            return true;
        }
        break;
    }
    return false;
}

static uint8_t calculate_checksum(const struct frame_parser *p_parser)
{
    uint8_t checksum = (uint8_t)p_parser->type;

    for (uint8_t i = 0; i < p_parser->payload_length; i++) {
        checksum ^= p_parser->payload[i];
    }

    return checksum;
}