#ifndef UART_RX_H
#define UART_RX_H

int uart_rx_init(void);

enum rx_overflow_policy {
    // 기존 버퍼와 불완전한 프레임을 모두 폐기한다.
    RX_OVERFLOW_DISCARD_ALL,
    // 기존 버퍼에서 완성 가능한 프레임을 처리한 뒤 남은 부분을 폐기한다.
    RX_OVERFLOW_DRAIN_BUFFER,
};

// 빌드 시 적용할 정책. drain을 시험하려면 RX_OVERFLOW_DRAIN_BUFFER로 변경한다.
#define RX_OVERFLOW_POLICY RX_OVERFLOW_DISCARD_ALL

#endif
