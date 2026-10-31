#include "frame_parser.h"

#define INIT_CRC 0xFFFF

static bool frame_crc_is_valid(const struct frame_parser *p_parser);
static uint16_t crc16_update(uint16_t crc, const uint8_t byte);


void parser_init(struct frame_parser *p_parser) {
    p_parser->state = WAIT_STX;
    p_parser->payload_length = 0;
    p_parser->payload_index = 0;
    p_parser->type = 0;
}

ParserResult parser_feed(struct frame_parser *p_parser, uint8_t byte) {
    switch (p_parser->state) {
    case WAIT_STX:
        if (byte == STX) {
            p_parser->state = WAIT_LEN;
        }
        break;
    case WAIT_LEN:
        if (byte == 0 || byte > MAX_PAYLOAD) {
            p_parser->state = WAIT_STX;
            return PARSER_LENGTH_ERROR;
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
            p_parser->state = WAIT_CRC_H;
        }
        break;
    case WAIT_CRC_H:
        p_parser->received_crc = (uint16_t)byte << 8;

        p_parser->state = WAIT_CRC_L;
        break;
    case WAIT_CRC_L:
        p_parser->received_crc |= byte;

        const bool valid = frame_crc_is_valid(p_parser);

        p_parser->state = WAIT_STX;
        p_parser->payload_index = 0;

        return valid? PARSER_FRAME_READY: PARSER_CRC_ERROR;
    }
    return PARSER_INCOMPLETE;
}

static bool frame_crc_is_valid(const struct frame_parser *p_parser)
{
    uint16_t crc = INIT_CRC;

    crc = crc16_update(crc, p_parser->payload_length);
    crc = crc16_update(crc, p_parser->type);

    for (uint8_t i = 0; i < p_parser->payload_length; ++i) {
        crc = crc16_update(crc, p_parser->payload[i]);
    }
    
    return (crc == p_parser->received_crc);
}

static uint16_t crc16_update(uint16_t crc, const uint8_t byte)
{
    crc ^= (uint16_t)byte << 8;

    for (int i = 0; i < 8; ++i) {
        if (crc & 0x8000) {
            crc = (uint16_t)((crc << 1) ^ 0x1021);
        } else {
            crc = (uint16_t)(crc << 1);
        }
    }

    return crc;
}
