#ifndef FRAME_PARSER_H
#define FRAME_PARSER_H

#include <stdbool.h>
#include <stdint.h>

#define STX 0x7E
#define MAX_PAYLOAD 32

typedef enum {
    WAIT_STX,
    WAIT_LEN,
    WAIT_TYPE,
    READ_PAYLOAD,
    WAIT_CRC
} ParserState;

typedef enum {
    TYPE_INIT       = 0x01,
    TYPE_RADAR_MEAS = 0x02,
    TYPE_STATUS     = 0x03,
    TYPE_COMMAND    = 0x04,
    TYPE_RESET      = 0x05,
} FrameType;

struct frame_parser {
    ParserState state;
    uint8_t payload_length;
    uint8_t payload_index;
    FrameType type;
    uint8_t payload[MAX_PAYLOAD];
};

void parser_init(struct frame_parser *p_parser);
bool parser_feed(struct frame_parser *p_parser, uint8_t byte);

#endif