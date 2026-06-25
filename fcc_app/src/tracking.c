/*
 * tracking.c — tracking 스레드 + 트랙 테이블
 *
 * 처리 흐름 (매 스캔 배치):
 *   1. 같은 timestamp_ms 측정값 수집
 *   2. 모든 활성 트랙 predict
 *   3. Greedy NN 데이터 연관 (Mahalanobis 게이팅)
 *   4. 연관 트랙: EKF update + M-of-N hit 카운트
 *   5. 미연관 트랙: coast 카운트 → 임계 초과 시 삭제
 *   6. 미연관 측정값: 새 tentative 트랙 개시
 *   7. confirmed 트랙만 TX
 */

#include "tracking.h"
#include "uart_rx.h"
#include "frame.h"
#include "kalman.h"
#include <zephyr/sys/printk.h>

/* 공유 트랙 테이블 */
struct track_table g_track_table;
K_MUTEX_DEFINE(g_track_mutex);

/* 다음 트랙 ID 카운터 (wrap-around 허용) */
static uint8_t g_next_id;

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

static int alloc_slot(void)
{
    for (int i = 0; i < MAX_TRACKS; i++) {
        if (!g_track_table.tracks[i].active) {
            return i;
        }
    }
    return -1;
}

static void init_track(int slot, const FccMeasPayload *mp)
{
    struct track *t   = &g_track_table.tracks[slot];
    kalman_init(&t->ekf, mp->range_m, mp->azimuth_rad);
    t->active         = true;
    t->id             = g_next_id++;
    t->state          = TRACK_TENTATIVE;
    t->hit_count      = 1;
    t->scan_count     = 1;
    t->coast_count    = 0;
    t->last_update_ms = mp->timestamp_ms;
}

static void process_scan(const FccMeasPayload *meas, int n_meas, bool tx_ready)
{
    float dist[MAX_MEAS_SCAN][MAX_TRACKS];
    int   matched_meas[MAX_TRACKS];
    bool  meas_used[MAX_MEAS_SCAN];
    uint32_t ts = meas[0].timestamp_ms;

    /* 1. predict + 거리 행렬 계산 */
    for (int j = 0; j < MAX_TRACKS; j++) {
        matched_meas[j] = -1;
        struct track *t = &g_track_table.tracks[j];
        if (!t->active) {
            for (int i = 0; i < n_meas; i++) {
                dist[i][j] = 1e9f;
            }
            continue;
        }
        float dt = (float)(ts - t->last_update_ms) / 1000.0f;
        if (dt > 0.0f) {
            kalman_predict(&t->ekf, dt);
        }
        for (int i = 0; i < n_meas; i++) {
            dist[i][j] = kalman_gate_sq(&t->ekf,
                                        meas[i].range_m,
                                        meas[i].azimuth_rad);
        }
    }
    for (int i = 0; i < n_meas; i++) {
        meas_used[i] = false;
    }

    /* 2. Greedy NN 연관: 게이트 내 최소 거리 쌍을 순차 선택 */
    for (;;) {
        float best = GATE_THRESH_SQ;
        int   bi = -1, bj = -1;
        for (int i = 0; i < n_meas; i++) {
            if (meas_used[i]) continue;
            for (int j = 0; j < MAX_TRACKS; j++) {
                if (!g_track_table.tracks[j].active) continue;
                if (matched_meas[j] >= 0) continue;
                if (dist[i][j] < best) {
                    best = dist[i][j];
                    bi   = i;
                    bj   = j;
                }
            }
        }
        if (bi < 0) break;
        matched_meas[bj] = bi;
        meas_used[bi]    = true;
    }

    /* 3. 트랙 갱신 + M-of-N 상태 전이 */
    for (int j = 0; j < MAX_TRACKS; j++) {
        struct track *t = &g_track_table.tracks[j];
        if (!t->active) continue;

        if (matched_meas[j] >= 0) {
            const FccMeasPayload *mp = &meas[matched_meas[j]];
            kalman_update(&t->ekf, mp->range_m, mp->azimuth_rad);
            t->hit_count++;
            t->coast_count = 0;
        } else {
            t->coast_count++;
        }

        if (t->scan_count < TRACK_N) {
            t->scan_count++;
        }
        t->last_update_ms = ts;

        if (t->state == TRACK_TENTATIVE && t->hit_count >= TRACK_M) {
            t->state = TRACK_CONFIRMED;
            printk("[tracking] track %u confirmed\n", t->id);
        }

        bool drop = false;
        if (t->coast_count > TRACK_COAST_MAX) {
            drop = true;
        }
        /* tentative가 N번 기회를 다 썼는데 M번 못 채우면 삭제 */
        if (t->state == TRACK_TENTATIVE &&
            t->scan_count >= TRACK_N &&
            t->hit_count  <  TRACK_M) {
            drop = true;
        }
        if (drop) {
            printk("[tracking] track %u deleted (coast=%u hit=%u/%u)\n",
                   t->id, t->coast_count, t->hit_count, t->scan_count);
            t->active = false;
        }
    }

    /* 4. 미연관 측정값 → 새 tentative 트랙 */
    for (int i = 0; i < n_meas; i++) {
        if (!meas_used[i]) {
            int slot = alloc_slot();
            if (slot >= 0) {
                init_track(slot, &meas[i]);
            }
        }
    }

    /* 5. confirmed 트랙 TX */
    if (tx_ready) {
        for (int j = 0; j < MAX_TRACKS; j++) {
            struct track *t = &g_track_table.tracks[j];
            if (!t->active || t->state != TRACK_CONFIRMED) continue;
            FccTrackPayload tp = {
                .timestamp_ms = ts,
                .track_id     = t->id,
                .x_m          = t->ekf.x[0],
                .y_m          = t->ekf.x[1],
                .vx_mps       = t->ekf.x[2],
                .vy_mps       = t->ekf.x[3],
            };
            fcc_send_track(&tp);
        }
    }
}

void tracking_thread(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

    const bool tx_ready = device_is_ready(g_hil_uart);
    static FccMeasPayload meas_buf[MAX_MEAS_SCAN];
    int      n_buf  = 0;
    uint32_t cur_ts = 0U;

    while (1) {
        FccMeasPayload mp;
        /* 200ms 타임아웃: 스캔 끝 감지 및 강제 flush 용 */
        int rc = k_msgq_get(&meas_msgq, &mp, K_MSEC(200));

        if (rc == 0) {
            /* 새 타임스탬프 → 이전 배치 처리 후 새 배치 시작 */
            if (n_buf > 0 && mp.timestamp_ms != cur_ts) {
                printk("[tracking] scan t=%u n=%d\n", cur_ts, n_buf);
                k_mutex_lock(&g_track_mutex, K_FOREVER);
                process_scan(meas_buf, n_buf, tx_ready);
                k_mutex_unlock(&g_track_mutex);
                n_buf = 0;
            }
            if (n_buf == 0) {
                cur_ts = mp.timestamp_ms;
            }
            if (n_buf < MAX_MEAS_SCAN) {
                meas_buf[n_buf++] = mp;
            }
        } else {
            /* 타임아웃: 미처리 배치 강제 flush */
            if (n_buf > 0) {
                printk("[tracking] flush t=%u n=%d\n", cur_ts, n_buf);
                k_mutex_lock(&g_track_mutex, K_FOREVER);
                process_scan(meas_buf, n_buf, tx_ready);
                k_mutex_unlock(&g_track_mutex);
                n_buf = 0;
            }
        }
    }
}
