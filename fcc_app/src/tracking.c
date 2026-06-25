/*
 * tracking.c — tracking 스레드 + 트랙 테이블
 * meas_msgq에서 FccMeasPayload 수신 → (TODO: Kalman) → 트랙 에코 TX
 */

#include "tracking.h"
#include "uart_rx.h"
#include "frame.h"
#include "kalman.h"
#include <zephyr/sys/printk.h>

/* 공유 트랙 테이블 */
struct track_table g_track_table;
K_MUTEX_DEFINE(g_track_mutex);

/* TrackPayload를 프레임으로 인코딩하여 폴링 TX */
static void fcc_send_track(const FccTrackPayload *tp)
{
    /* TODO: uart_poll_out은 바이트당 busy-wait (~1.8ms/프레임 @ 115200).
     * Kalman 포팅 후 지터 문제 시 TX 큐 + 별도 스레드로 분리할 것. */
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

    const bool tx_ready = device_is_ready(g_hil_uart);
    FccMeasPayload mp;
    uint32_t cnt = 0U;
    struct track *trk = &g_track_table.tracks[0];

    while (1) {
        k_msgq_get(&meas_msgq, &mp, K_FOREVER);

        printk("[tracking] #%u t=%u r=%.1f az=%.3f dop=%.1f\n",
               cnt, mp.timestamp_ms,
               (double)mp.range_m,
               (double)mp.azimuth_rad,
               (double)mp.doppler_mps);

        /* 범위: 단일 트랙(track 0)만 운용. 데이터 연관/다중 트랙은
         * 다음 단계(트랙 관리)에서 추가. 들어오는 모든 측정값을
         * 이 트랙에 그대로 적용한다 (클러터 대응 없음). */
        k_mutex_lock(&g_track_mutex, K_FOREVER);
        if (!trk->active) {
            kalman_init(&trk->ekf, mp.range_m, mp.azimuth_rad);
            trk->active = true;
            trk->id = 0U;
        } else {
            float dt = (float)(mp.timestamp_ms - trk->last_update_ms) / 1000.0f;
            if (dt > 0.0f) {
                kalman_predict(&trk->ekf, dt);
            }
            kalman_update(&trk->ekf, mp.range_m, mp.azimuth_rad);
        }
        trk->last_update_ms = mp.timestamp_ms;

        FccTrackPayload tp = {
            .timestamp_ms = mp.timestamp_ms,
            .track_id     = trk->id,
            .x_m          = trk->ekf.x[0],
            .y_m          = trk->ekf.x[1],
            .vx_mps       = trk->ekf.x[2],
            .vy_mps       = trk->ekf.x[3],
        };
        k_mutex_unlock(&g_track_mutex);

        if (tx_ready) {
            fcc_send_track(&tp);
        }

        cnt++;
    }
}
