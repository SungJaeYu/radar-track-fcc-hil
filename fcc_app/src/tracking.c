/*
 * tracking.c — tracking 스레드 + 트랙 테이블
 * meas_msgq에서 FccMeasPayload 수신 → (TODO: Kalman) → 트랙 에코 TX
 */

#include "tracking.h"
#include "uart_rx.h"
#include "frame.h"
#include <zephyr/sys/printk.h>

/* 공유 트랙 테이블 */
struct track_table g_track_table;
K_MUTEX_DEFINE(g_track_mutex);

/* TrackPayload를 프레임으로 인코딩하여 폴링 TX */
static void fcc_send_track(const FccTrackPayload *tp)
{
    uint8_t  buf[5U + sizeof(FccTrackPayload)];
    uint16_t n = fcc_encode_frame(FCC_MSG_TRACK,
                                  (const uint8_t *)tp, sizeof(*tp),
                                  buf, sizeof(buf));
    for (uint16_t i = 0; i < n; i++) {
        uart_poll_out(g_hil_uart, buf[i]);
    }
}

void tracking_thread(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

    FccMeasPayload mp;
    uint32_t cnt = 0U;

    while (1) {
        k_msgq_get(&meas_msgq, &mp, K_FOREVER);

        printk("[tracking] #%u t=%u r=%.1f az=%.3f dop=%.1f\n",
               cnt, mp.timestamp_ms,
               (double)mp.range_m,
               (double)mp.azimuth_rad,
               (double)mp.doppler_mps);

        /* TODO: 칼만 필터 업데이트 + 트랙 관리 후 g_track_table 갱신
         * 임시: range를 x에 그대로 넣은 더미 에코 */
        if (device_is_ready(g_hil_uart)) {
            FccTrackPayload tp = {
                .timestamp_ms = mp.timestamp_ms,
                .track_id     = 0U,
                .x_m          = mp.range_m,
                .y_m          = 0.0f,
                .vx_mps       = 0.0f,
                .vy_mps       = 0.0f,
            };
            fcc_send_track(&tp);
        }

        cnt++;
    }
}
