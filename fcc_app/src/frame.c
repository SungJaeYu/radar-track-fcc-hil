/*
 * frame.c — HIL 바이너리 프레임 코덱 구현
 * Python protocol.py 의 crc16_ccitt / encode_frame / FrameParser 를 C로 포팅.
 */

#include "frame.h"
#include <zephyr/toolchain.h>
#include <string.h>

/* host 유닛테스트 이식성: 보드는 Zephyr __ASSERT, 그 외는 표준 assert */
#ifdef __ZEPHYR__
#include <zephyr/sys/__assert.h>
#else
#include <assert.h>
#define __ASSERT(cond, ...) assert(cond)
#endif

/* 페이로드 크기 컴파일 타임 검증 (Python struct calcsize 와 일치해야 함) */
BUILD_ASSERT(sizeof(FccMeasPayload)  == 20, "FccMeasPayload 크기 불일치");
BUILD_ASSERT(sizeof(FccTrackPayload) == 21, "FccTrackPayload 크기 불일치");
BUILD_ASSERT(sizeof(FccCtrlPayload)  ==  1, "FccCtrlPayload 크기 불일치");

/* ── CRC16-CCITT ──────────────────────────────────────────────────────────── */

uint16_t fcc_crc16(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0x0000U;

    for (uint16_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int j = 0; j < 8; j++) {
            crc = (crc & 0x8000U)
                  ? (uint16_t)((crc << 1) ^ 0x1021U)
                  : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

/* ── 프레임 인코더 ─────────────────────────────────────────────────────────── */

uint16_t fcc_encode_frame(uint8_t msg_type,
                          const uint8_t *payload, uint8_t payload_len,
                          uint8_t *buf, uint16_t buf_size)
{
    __ASSERT(buf != NULL, "fcc_encode_frame: buf is NULL");
    __ASSERT(payload_len <= FCC_MAX_PAYLOAD,
             "fcc_encode_frame: payload_len %u > max", payload_len);

    /* release(ASSERT=n)에서도 사는 하드 가드: crc_in 스택 오버런 방지 */
    if (payload_len > FCC_MAX_PAYLOAD) {
        return 0;
    }

    uint16_t total = 5U + payload_len;

    if (buf_size < total) {
        return 0;
    }

    /* CRC 입력: [LEN, TYPE, PAYLOAD...] */
    uint8_t crc_in[2U + FCC_MAX_PAYLOAD];
    crc_in[0] = payload_len;
    crc_in[1] = msg_type;
    memcpy(&crc_in[2], payload, payload_len);
    uint16_t crc = fcc_crc16(crc_in, 2U + payload_len);

    buf[0] = FCC_STX;
    buf[1] = payload_len;
    buf[2] = msg_type;
    memcpy(&buf[3], payload, payload_len);
    buf[3U + payload_len]     = (uint8_t)(crc >> 8);   /* CRC big-endian */
    buf[3U + payload_len + 1U] = (uint8_t)(crc & 0xFFU);

    return total;
}

/* ── FrameParser 상태머신 ─────────────────────────────────────────────────── */

void frame_parser_init(FrameParser *p)
{
    p->state   = FP_WAIT_STX;
    p->buf_idx = 0;
    p->len     = 0;
    p->msg_type = 0;
    p->crc_h   = 0;
}

void frame_parser_reset(FrameParser *p)
{
    p->state    = FP_WAIT_STX;
    p->buf_idx  = 0;
    p->len      = 0;
    p->msg_type = 0;
    p->crc_h    = 0;
}

bool frame_parser_feed(FrameParser *p, uint8_t b, FccFrame *out)
{
    __ASSERT(p != NULL, "frame_parser_feed: p is NULL");
    __ASSERT(out != NULL, "frame_parser_feed: out is NULL");

    switch (p->state) {

    case FP_WAIT_STX:
        if (b == FCC_STX) {
            p->state = FP_WAIT_LEN;
        }
        break;

    case FP_WAIT_LEN:
        if (b > FCC_MAX_PAYLOAD) {
            /* 비정상적으로 큰 LEN → 이 STX는 가비지, 재동기화 */
            p->state = FP_WAIT_STX;
        } else {
            p->len   = b;
            p->state = FP_WAIT_TYPE;
        }
        break;

    case FP_WAIT_TYPE:
        p->msg_type = b;
        p->buf_idx  = 0;
        p->state = (p->len > 0U) ? FP_WAIT_PAYLOAD : FP_WAIT_CRC_H;
        break;

    case FP_WAIT_PAYLOAD:
        __ASSERT(p->buf_idx < FCC_MAX_PAYLOAD,
                 "frame_parser_feed: buf overrun (idx=%u)", p->buf_idx);
        p->buf[p->buf_idx++] = b;
        if (p->buf_idx == p->len) {
            p->state = FP_WAIT_CRC_H;
        }
        break;

    case FP_WAIT_CRC_H:
        p->crc_h = b;
        p->state  = FP_WAIT_CRC_L;
        break;

    case FP_WAIT_CRC_L: {
        uint16_t received = ((uint16_t)p->crc_h << 8) | b;

        uint8_t crc_in[2U + FCC_MAX_PAYLOAD];
        crc_in[0] = p->len;
        crc_in[1] = p->msg_type;
        memcpy(&crc_in[2], p->buf, p->buf_idx);
        uint16_t expected = fcc_crc16(crc_in, 2U + p->buf_idx);

        p->state = FP_WAIT_STX;

        if (received == expected) {
            if (out != NULL) {
                out->msg_type    = p->msg_type;
                out->payload_len = p->buf_idx;
                memcpy(out->payload, p->buf, p->buf_idx);
            }
            return true;
        }
        /* CRC 불일치 → 프레임 버리고 재동기화 */
        break;
    }
    }

    return false;
}
