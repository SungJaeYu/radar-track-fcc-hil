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

#define MAX_TRACKS 8

struct track {
    bool    active;
    uint8_t id;
    /* TODO: 칼만 상태벡터(x, y, vx, vy), 공분산, M-of-N 카운터 */
};

struct track_table {
    struct track tracks[MAX_TRACKS];
};

extern struct track_table g_track_table;
extern struct k_mutex     g_track_mutex;

/* 스레드 진입점 */
void tracking_thread(void *p1, void *p2, void *p3);
