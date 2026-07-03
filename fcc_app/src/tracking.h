#pragma once
/*
 * tracking.h — 트랙 테이블 + tracking 스레드 공개 인터페이스
 *
 * 트랙 테이블(g_track_table)은 g_track_mutex로 보호.
 * display_thread는 mutex 잠금 후 read-only 접근.
 */

#include <zephyr/kernel.h>
#include <stdbool.h>
#include <stdint.h>
#include "kalman.h"

#define MAX_TRACKS      8
#define MAX_MEAS_SCAN   16  /* 스캔당 최대 측정값 수 */

/* M-of-N 파라미터: N번 스캔 기회 중 M번 이상 hit → confirmed */
#define TRACK_M         2
#define TRACK_N         3
#define TRACK_COAST_MAX 3   /* 연속 미연관 허용 횟수 초과 시 삭제 */

/* chi-square 2-dof 95% 게이트 임계값 */
#define GATE_THRESH_SQ  5.99f

typedef enum {
    TRACK_TENTATIVE = 0,
    TRACK_CONFIRMED,
} track_state_t;

struct track {
    bool          active;
    uint8_t       id;
    struct kalman_state ekf;
    uint32_t      last_update_ms;
    track_state_t state;
    uint8_t       hit_count;    /* 생성 후 누적 hit 수 */
    uint8_t       scan_count;   /* 생성 후 경과 스캔 수 (TRACK_N 포화) */
    uint8_t       coast_count;  /* 연속 미연관 횟수 */
};

struct track_table {
    struct track tracks[MAX_TRACKS];
};

extern struct track_table g_track_table;
extern struct k_mutex     g_track_mutex;

/* display 스레드에 넘길 HIL 통계 스냅샷 (g_stats_mutex로 보호) */
struct hil_stats {
    uint32_t frames_rx;         /* 누적 수신 측정 프레임 수 */
    bool     has_meas;          /* 측정 1건 이상 수신했는가 */
    uint32_t last_ts_ms;        /* 마지막 측정 timestamp_ms */
    float    last_range_m;
    float    last_azimuth_rad;
    float    last_doppler_mps;
};

extern struct hil_stats g_hil_stats;
extern struct k_mutex   g_stats_mutex;

/* 스레드 진입점 */
void tracking_thread(void *p1, void *p2, void *p3);
