/*
 * display.c — display 스레드 (LVGL)
 * F746G-DISCO 온보드 LCD에 HIL 상태판 렌더.
 * 이 단계: 정적 "RADAR FCC (HIL)" 라벨만 표시 (LVGL 배선 검증용).
 */

#include "display.h"
#include "tracking.h"
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/sys/printk.h>
#include <lvgl.h>
#include <stdio.h>
#include <math.h>

#define RAD2DEG(r) ((r) * 180.0f / (float)M_PI)

void display_thread(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

    const struct device *disp = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
    if (!device_is_ready(disp)) {
        printk("[display ] LCD 디바이스 미준비 — 렌더 중단\n");
        return;
    }

    /* 좌상단 정렬 멀티라인 라벨 1개
     * 주의: Zephyr v4.2는 LVGL v9를 번들함 — v8 이전 API인
     * lv_scr_act()/lv_task_handler()는 기본 설정(LV_USE_OBSOLETE_API=n)에서
     * 존재하지 않음. v9 API인 lv_screen_active()/lv_timer_handler() 사용. */
    lv_obj_t *label = lv_label_create(lv_screen_active());
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 8, 8);
    lv_label_set_text(label, "RADAR FCC (HIL)\nbooting...");

    display_blanking_off(disp);

    char buf[256];

    while (1) {
        /* 스냅샷 복사 (락 최소 구간) */
        struct hil_stats s;
        k_mutex_lock(&g_stats_mutex, K_FOREVER);
        s = g_hil_stats;
        k_mutex_unlock(&g_stats_mutex);

        if (s.has_meas) {
            snprintf(buf, sizeof(buf),
                     "RADAR FCC (HIL)\n"
                     "frames rx : %u\n"
                     "last meas\n"
                     "  t   : %u ms\n"
                     "  rng : %.1f m\n"
                     "  az  : %.1f deg\n"
                     "  dop : %.1f m/s\n"
                     "tracks: 0 (kalman TODO)",
                     s.frames_rx, s.last_ts_ms,
                     (double)s.last_range_m,
                     (double)RAD2DEG(s.last_azimuth_rad),
                     (double)s.last_doppler_mps);
        } else {
            snprintf(buf, sizeof(buf),
                     "RADAR FCC (HIL)\n"
                     "frames rx : 0\n"
                     "last meas\n"
                     "  t   : --\n"
                     "  rng : --\n"
                     "  az  : --\n"
                     "  dop : --\n"
                     "tracks: 0 (kalman TODO)");
        }
        lv_label_set_text(label, buf);

        lv_timer_handler();
        k_sleep(K_MSEC(100));
    }
}
