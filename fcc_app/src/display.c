/*
 * display.c — display 스레드
 * g_track_mutex 잠금 → g_track_table read-only → (TODO: LVGL PPI 화면 갱신)
 * 현재: 2초마다 활성 트랙 수 로그 출력
 */

#include "display.h"
#include "tracking.h"
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

void display_thread(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

    while (1) {
        k_mutex_lock(&g_track_mutex, K_FOREVER);
        uint8_t active = 0U;

        for (int i = 0; i < MAX_TRACKS; i++) {
            if (g_track_table.tracks[i].active) {
                active++;
            }
        }
        k_mutex_unlock(&g_track_mutex);

        printk("[display ] 활성 트랙: %u\n", active);
        k_sleep(K_MSEC(2000));
    }
}
