#ifndef FRAME_PARSER_H
#define FRAME_PARSER_H

#include <stdbool.h>
#include <stdint.h>

#include "protocol/protocol_frame.h"

#define STX 0x7E

typedef enum {
    PARSER_STATE_WAIT_STX,
    PARSER_STATE_WAIT_LEN,
    PARSER_STATE_WAIT_TYPE,
    PARSER_STATE_READ_PAYLOAD,
    PARSER_STATE_WAIT_CRC_H,
    PARSER_STATE_WAIT_CRC_L
} ParserState;

typedef enum {
    // No complete frame yet; wait for more bytes.
    PARSER_RESULT_INCOMPLETE,
    // A complete frame passed CRC validation and may be copied by the caller.
    PARSER_RESULT_FRAME_READY,
    // Reject the assembled frame and resume searching for STX.
    PARSER_RESULT_CRC_ERROR,
    // Reject an out-of-range LEN and resume searching for STX.
    PARSER_RESULT_LENGTH_ERROR,
} ParserResult;

// Mutable assembly state owned by one caller; concurrent feeds are not supported.
struct frame_parser {
    ParserState state;
    struct protocol_frame frame;
    uint8_t payload_index;
    uint16_t received_crc;
};

// Require a non-NULL parser. Reset assembly state, length, type, and payload index.
// Does not erase the payload array or received_crc; they are overwritten as needed.
void parser_init(struct frame_parser *p_parser);
// Feed one byte in stream order into an initialized, non-NULL parser.
// Use parser.frame only on PARSER_RESULT_FRAME_READY and copy it before feeding more bytes.
// Length/CRC errors discard that frame; later bytes are searched for a new STX.
// Does not validate payload semantics or handle inactivity timeouts.
ParserResult parser_feed(struct frame_parser *p_parser, uint8_t byte);

#endif