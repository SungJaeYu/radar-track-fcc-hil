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

/* HIL 통계 스냅샷 (display 스레드가 read) */
struct hil_stats g_hil_stats;
K_MUTEX_DEFINE(g_stats_mutex);

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

    while (1) {
        k_msgq_get(&meas_msgq, &mp, K_FOREVER);

        printk("[tracking] #%u t=%u r=%.1f az=%.3f dop=%.1f\n",
               cnt, mp.timestamp_ms,
               (double)mp.range_m,
               (double)mp.azimuth_rad,
               (double)mp.doppler_mps);

        /* display용 통계 스냅샷 갱신 */
        k_mutex_lock(&g_stats_mutex, K_FOREVER);
        g_hil_stats.frames_rx        = cnt + 1U;
        g_hil_stats.has_meas         = true;
        g_hil_stats.last_ts_ms       = mp.timestamp_ms;
        g_hil_stats.last_range_m     = mp.range_m;
        g_hil_stats.last_azimuth_rad = mp.azimuth_rad;
        g_hil_stats.last_doppler_mps = mp.doppler_mps;
        k_mutex_unlock(&g_stats_mutex);

        /* TODO: 칼만 필터 업데이트 + 트랙 관리 후 g_track_table 갱신
         * 주의: 현재 g_track_table은 갱신되지 않으므로 display_thread는 항상 "활성 트랙: 0" 출력
         * 임시: range를 x에 그대로 넣은 더미 에코 */
        if (tx_ready) {
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
