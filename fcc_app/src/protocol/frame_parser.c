#include "protocol/frame_parser.h"

#include <zephyr/sys/__assert.h>

static bool frame_crc_is_valid(const struct frame_parser *p_parser);

void parser_init(struct frame_parser *p_parser)
{
    __ASSERT(p_parser != NULL, "Parser pointer is NULL");

    p_parser->state = PARSER_STATE_WAIT_STX;
    p_parser->frame.payload_length = 0;
    p_parser->frame.type = 0;
    p_parser->payload_index = 0;
}

ParserResult parser_feed(struct frame_parser *p_parser, uint8_t byte)
{
    __ASSERT(p_parser != NULL, "Parser pointer is NULL");

    switch (p_parser->state) {
    case PARSER_STATE_WAIT_STX:
        if (byte == PROTOCOL_STX) {
            p_parser->state = PARSER_STATE_WAIT_LEN;
        }
        break;
    case PARSER_STATE_WAIT_LEN:
        if (byte == 0 || byte > PROTOCOL_MAX_PAYLOAD) {
            p_parser->state = PARSER_STATE_WAIT_STX;
            return PARSER_RESULT_LENGTH_ERROR;
        } else {
            p_parser->frame.payload_length = byte;
            p_parser->state = PARSER_STATE_WAIT_TYPE;
        }
        break;
    case PARSER_STATE_WAIT_TYPE:
        p_parser->frame.type = byte;
        p_parser->state = PARSER_STATE_READ_PAYLOAD;
        break;
    // PROTOCOL_STX inside payload is ordinary data; framing is controlled by the validated LEN.
    case PARSER_STATE_READ_PAYLOAD:
        __ASSERT(
            p_parser->frame.payload_length > 0 &&
            p_parser->frame.payload_length <= PROTOCOL_MAX_PAYLOAD,
            "Invalid internal payload length: %u", (unsigned int)p_parser->frame.payload_length
        );

        __ASSERT(
            p_parser->payload_index < p_parser->frame.payload_length,
            "Payload index out of range: index=%u len=%u",
            (unsigned int)p_parser->payload_index,
            (unsigned int)p_parser->frame.payload_length
        );

        p_parser->frame.payload[p_parser->payload_index++] = byte;

        if (p_parser->payload_index == p_parser->frame.payload_length) {
            p_parser->state = PARSER_STATE_WAIT_CRC_H;
        }
        break;
    case PARSER_STATE_WAIT_CRC_H:
        p_parser->received_crc = (uint16_t)byte << 8;

        p_parser->state = PARSER_STATE_WAIT_CRC_L;
        break;
    case PARSER_STATE_WAIT_CRC_L: {
        p_parser->received_crc |= byte;

        const bool valid = frame_crc_is_valid(p_parser);

        // Reset assembly control for the next frame but retain this frame for the caller.
        p_parser->state = PARSER_STATE_WAIT_STX;
        p_parser->payload_index = 0;

        return valid ? PARSER_RESULT_FRAME_READY : PARSER_RESULT_CRC_ERROR;
    }
    default:
        __ASSERT(0, "Invalid parser state: %d", (int)p_parser->state);
        parser_init(p_parser);
        break;
    }
    return PARSER_RESULT_INCOMPLETE;
}

static bool frame_crc_is_valid(const struct frame_parser *p_parser)
{
    return protocol_frame_crc(&p_parser->frame) == p_parser->received_crc;
}
