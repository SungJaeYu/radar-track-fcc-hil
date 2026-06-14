#pragma once
/*
 * frame.h — HIL 바이너리 프레임 코덱 (Zephyr 비의존, 순수 C)
 *
 * 프레임 포맷:  STX(1) | LEN(1) | TYPE(1) | PAYLOAD(LEN) | CRC16-BE(2)
 * CRC 범위: [LEN, TYPE, PAYLOAD...] (STX 제외)
 * 페이로드 엔디언: little-endian (ARM Cortex-M 기본)
 */

#include <stdbool.h>
#include <stdint.h>

/* ── 프레임 상수 ──────────────────────────────────────── */

#define FCC_STX          0x7EU

#define FCC_MSG_MEAS     0x01U  /* PC → STM32: 레이더 측정값 */
#define FCC_MSG_TRACK    0x02U  /* STM32 → PC: 트랙 상태 */
#define FCC_MSG_CTRL     0x03U  /* 양방향: 제어 명령 */

#define FCC_CTRL_START   0x01U
#define FCC_CTRL_STOP    0x02U
#define FCC_CTRL_RESET   0x03U

/* 현재 최대 페이로드 = TRACK 21바이트. 여유를 두어 64로 설정.
 * LEN > FCC_MAX_PAYLOAD 이면 해당 STX는 가비지로 간주 */
#define FCC_MAX_PAYLOAD  64U

/* ── 페이로드 구조체 ──────────────────────────────────── */
/* Python struct '<'(패딩 없음)과 동일 레이아웃을 위해 packed 필수 */

typedef struct __attribute__((packed)) {
    uint32_t timestamp_ms;
    float    range_m;
    float    azimuth_rad;
    float    elevation_rad;
    float    doppler_mps;
} FccMeasPayload;   /* 20바이트 */

typedef struct __attribute__((packed)) {
    uint32_t timestamp_ms;
    uint8_t  track_id;
    float    x_m;
    float    y_m;
    float    vx_mps;
    float    vy_mps;
} FccTrackPayload;  /* 21바이트 */

typedef struct __attribute__((packed)) {
    uint8_t cmd;
} FccCtrlPayload;   /* 1바이트 */

/* ── CRC16-CCITT (XModem: poly=0x1021, init=0x0000) ──── */

uint16_t fcc_crc16(const uint8_t *data, uint16_t len);

/* ── 프레임 인코더 ───────────────────────────────────── */
/* buf에 완성된 프레임을 기록. 반환값: 프레임 총 바이트 수 (실패 시 0) */
uint16_t fcc_encode_frame(uint8_t msg_type,
                          const uint8_t *payload, uint8_t payload_len,
                          uint8_t *buf, uint16_t buf_size);

/* ── FrameParser 상태머신 ───────────────────────────── */

typedef enum {
    FP_WAIT_STX = 0,
    FP_WAIT_LEN,
    FP_WAIT_TYPE,
    FP_WAIT_PAYLOAD,
    FP_WAIT_CRC_H,
    FP_WAIT_CRC_L,
} FpState;

typedef struct {
    FpState  state;
    uint8_t  len;
    uint8_t  msg_type;
    uint8_t  buf[FCC_MAX_PAYLOAD];
    uint8_t  buf_idx;
    uint8_t  crc_h;
} FrameParser;

typedef struct {
    uint8_t msg_type;
    uint8_t payload[FCC_MAX_PAYLOAD];
    uint8_t payload_len;
} FccFrame;

void frame_parser_init(FrameParser *p);
void frame_parser_reset(FrameParser *p);

/* 바이트 1개를 파서에 공급. 프레임 완성 시 out을 채우고 true 반환. */
bool frame_parser_feed(FrameParser *p, uint8_t byte, FccFrame *out);
