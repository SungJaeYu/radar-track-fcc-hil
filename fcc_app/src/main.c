#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/ring_buffer.h>
#include <string.h>

#include "frame.h"

/* ───────────────────────────────────────────────────────────────────────────
 * HIL UART 장치 (USART6: Arduino D0/D1)
 * 하드웨어: TX=PG14(D1), RX=PG9(D0) → USB-UART 어댑터 → PC /dev/ttyUSB0
 * ─────────────────────────────────────────────────────────────────────────── */
static const struct device *g_hil_uart;

/* ISR ↔ uart_rx_thread 간 바이트 버퍼 */
#define HIL_RX_BUF_SIZE 256U
RING_BUF_DECLARE(g_uart_rx_ring, HIL_RX_BUF_SIZE);
static K_SEM_DEFINE(g_uart_rx_sem, 0, 1);

/* ───────────────────────────────────────────────────────────────────────────
 * 측정값 메시지: uart_rx_thread가 넣고, tracking_thread가 꺼낸다
 * ─────────────────────────────────────────────────────────────────────────── */
struct meas_msg {
    uint32_t timestamp_ms;
    float    range_m;
    float    azimuth_rad;
    float    elevation_rad;
    float    doppler_mps;
};

K_MSGQ_DEFINE(meas_msgq, sizeof(struct meas_msg), 8, 4);

/* ───────────────────────────────────────────────────────────────────────────
 * 공유 트랙 테이블: tracking_thread가 쓰고, display_thread가 읽는다
 * ─────────────────────────────────────────────────────────────────────────── */
#define MAX_TRACKS 8

struct track {
    bool     active;
    uint8_t  id;
    /* TODO: 칼만 상태벡터(x, y, vx, vy), 공분산, M-of-N 카운터 */
};

struct track_table {
    struct track tracks[MAX_TRACKS];
};

static struct track_table g_track_table;
static K_MUTEX_DEFINE(g_track_mutex);

/* ───────────────────────────────────────────────────────────────────────────
 * HIL UART ISR: FIFO에서 바이트 읽어 링버퍼에 적재, 스레드 깨움
 * ─────────────────────────────────────────────────────────────────────────── */
static void hil_uart_isr(const struct device *dev, void *user_data)
{
    ARG_UNUSED(user_data);
    uint8_t  byte;
    bool     got_data = false;

    while (uart_irq_update(dev) && uart_irq_is_pending(dev)) {
        if (!uart_irq_rx_ready(dev)) {
            break;
        }
        while (uart_fifo_read(dev, &byte, 1) > 0) {
            ring_buf_put(&g_uart_rx_ring, &byte, 1);
            got_data = true;
        }
    }

    if (got_data) {
        k_sem_give(&g_uart_rx_sem);
    }
}

/* ───────────────────────────────────────────────────────────────────────────
 * HIL 송신 헬퍼: TrackPayload를 프레임으로 인코딩하여 폴링 TX
 * ─────────────────────────────────────────────────────────────────────────── */
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

/* ───────────────────────────────────────────────────────────────────────────
 * 스레드 스택 & 우선순위
 * ─────────────────────────────────────────────────────────────────────────── */
#define STACK_UART_RX  2048
#define STACK_TRACKING 4096
#define STACK_DISPLAY  2048

K_THREAD_STACK_DEFINE(uart_rx_stack,  STACK_UART_RX);
K_THREAD_STACK_DEFINE(tracking_stack, STACK_TRACKING);
K_THREAD_STACK_DEFINE(display_stack,  STACK_DISPLAY);

static struct k_thread uart_rx_td;
static struct k_thread tracking_td;
static struct k_thread display_td;

/* ───────────────────────────────────────────────────────────────────────────
 * UART RX 스레드
 * USART6에서 바이트 수신 → FrameParser → meas_msgq에 put
 * CTRL 프레임(RESET 등)도 여기서 처리
 * ─────────────────────────────────────────────────────────────────────────── */
static void uart_rx_thread(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

    FrameParser parser;
    FccFrame    frame;

    frame_parser_init(&parser);

    if (!device_is_ready(g_hil_uart)) {
        printk("[uart_rx ] USART6 준비 안됨 - HIL 비활성\n");
        return;
    }

    uart_irq_callback_set(g_hil_uart, hil_uart_isr);
    uart_irq_rx_enable(g_hil_uart);

    printk("[uart_rx ] USART6 준비 완료, HIL 수신 시작\n");

    while (1) {
        /* 데이터 도착까지 대기 */
        k_sem_take(&g_uart_rx_sem, K_FOREVER);

        uint8_t byte;

        while (ring_buf_get(&g_uart_rx_ring, &byte, 1) == 1) {
            if (!frame_parser_feed(&parser, byte, &frame)) {
                continue;
            }

            /* ── 완성 프레임 처리 ── */
            if (frame.msg_type == FCC_MSG_MEAS &&
                frame.payload_len == sizeof(FccMeasPayload)) {

                const FccMeasPayload *mp = (const FccMeasPayload *)frame.payload;
                struct meas_msg msg = {
                    .timestamp_ms  = mp->timestamp_ms,
                    .range_m       = mp->range_m,
                    .azimuth_rad   = mp->azimuth_rad,
                    .elevation_rad = mp->elevation_rad,
                    .doppler_mps   = mp->doppler_mps,
                };

                if (k_msgq_put(&meas_msgq, &msg, K_NO_WAIT) != 0) {
                    /* 큐 포화 → 가장 오래된 항목 제거 후 재시도 */
                    k_msgq_purge(&meas_msgq);
                    printk("[uart_rx ] meas_msgq 포화, 퍼지\n");
                }

            } else if (frame.msg_type == FCC_MSG_CTRL &&
                       frame.payload_len == sizeof(FccCtrlPayload)) {

                const FccCtrlPayload *cp = (const FccCtrlPayload *)frame.payload;

                if (cp->cmd == FCC_CTRL_RESET) {
                    frame_parser_reset(&parser);
                    k_msgq_purge(&meas_msgq);
                    printk("[uart_rx ] CTRL_RESET\n");
                }
            }
        }
    }
}

/* ───────────────────────────────────────────────────────────────────────────
 * Tracking 스레드
 * meas_msgq에서 측정값 꺼냄 → (TODO: 게이팅 → 데이터연관 → 칼만 → M-of-N)
 * 지금은 측정값 수신 확인 로그 + 더미 트랙 에코
 * ─────────────────────────────────────────────────────────────────────────── */
static void tracking_thread(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

    struct meas_msg msg;
    uint32_t cnt = 0U;

    while (1) {
        k_msgq_get(&meas_msgq, &msg, K_FOREVER);

        printk("[tracking] #%u t=%u r=%.1f az=%.3f dop=%.1f\n",
               cnt, msg.timestamp_ms,
               (double)msg.range_m,
               (double)msg.azimuth_rad,
               (double)msg.doppler_mps);

        /* TODO: 칼만 필터 업데이트 + 트랙 관리 후 g_track_table 갱신
         * 임시: 측정값 수신을 확인하기 위해 더미 트랙 에코
         * (x, y는 극→직교 변환 없이 range를 x에 그대로 넣음) */
        if (device_is_ready(g_hil_uart)) {
            FccTrackPayload tp = {
                .timestamp_ms = msg.timestamp_ms,
                .track_id     = 0U,
                .x_m          = msg.range_m,   /* 임시: 나중에 칼만 추정값으로 교체 */
                .y_m          = 0.0f,
                .vx_mps       = 0.0f,
                .vy_mps       = 0.0f,
            };
            fcc_send_track(&tp);
        }

        cnt++;
    }
}

/* ───────────────────────────────────────────────────────────────────────────
 * Display 스레드
 * g_track_mutex 잠금 → g_track_table read-only → (TODO: LVGL PPI 화면 갱신)
 * 지금은 2초마다 활성 트랙 수 로그만 출력
 * ─────────────────────────────────────────────────────────────────────────── */
static void display_thread(void *p1, void *p2, void *p3)
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

/* ───────────────────────────────────────────────────────────────────────────
 * main: UART 장치 확인 후 3개 스레드 생성
 * ─────────────────────────────────────────────────────────────────────────── */
int main(void)
{
    printk("=== FCC HIL booting (UART 프레이밍 단계) ===\n");

    g_hil_uart = DEVICE_DT_GET(DT_NODELABEL(usart6));
    if (!device_is_ready(g_hil_uart)) {
        printk("[main] 경고: USART6 준비 안됨\n");
    }

    k_thread_create(&uart_rx_td, uart_rx_stack,
                    K_THREAD_STACK_SIZEOF(uart_rx_stack),
                    uart_rx_thread, NULL, NULL, NULL,
                    5, 0, K_NO_WAIT);
    k_thread_name_set(&uart_rx_td, "uart_rx");

    k_thread_create(&tracking_td, tracking_stack,
                    K_THREAD_STACK_SIZEOF(tracking_stack),
                    tracking_thread, NULL, NULL, NULL,
                    6, 0, K_NO_WAIT);
    k_thread_name_set(&tracking_td, "tracking");

    k_thread_create(&display_td, display_stack,
                    K_THREAD_STACK_SIZEOF(display_stack),
                    display_thread, NULL, NULL, NULL,
                    7, 0, K_NO_WAIT);
    k_thread_name_set(&display_td, "display");

    return 0;
}
