#ifndef FRAME_PARSER_H
#define FRAME_PARSER_H

#include <stdbool.h>
#include <stdint.h>

#include "protocol_frame.h"

#define STX 0x7E

typedef enum {
    WAIT_STX,
    WAIT_LEN,
    WAIT_TYPE,
    READ_PAYLOAD,
    WAIT_CRC_H,
    WAIT_CRC_L
} ParserState;

typedef enum {
    PARSER_INCOMPLETE,
    PARSER_FRAME_READY,
    PARSER_CRC_ERROR,
    PARSER_LENGTH_ERROR,
} ParserResult;

struct frame_parser {
    ParserState state;
    struct protocol_frame frame;
    uint8_t payload_index;
    uint16_t received_crc;
};

void parser_init(struct frame_parser *p_parser);
ParserResult parser_feed(struct frame_parser *p_parser, uint8_t byte);

#endif