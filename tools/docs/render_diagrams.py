"""Render documentation diagrams as PNG with the existing GUI dependencies."""
import argparse
import json
import os
from pathlib import Path
from xml.sax.saxutils import escape

os.environ.setdefault('QT_QPA_PLATFORM', 'offscreen')
from PySide6.QtCore import QByteArray
from PySide6.QtGui import QImage, QPainter
from PySide6.QtSvg import QSvgRenderer
from PySide6.QtWidgets import QApplication

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'docs/images'
INK = '#182c40'
MUTED = '#51677b'
BLUE = '#2563a8'
TEAL = '#087f83'
LANGUAGE = 'ko'
LABELS = {}


class Diagram:
    def __init__(self, width, height, title, subtitle):
        self.width, self.height = width, height
        self.parts = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
                      f'<rect width="{width}" height="{height}" fill="#f5f8fc"/>']
        self.text(36, 47, title, 30, INK, True)
        self.text(36, 81, subtitle, 19, MUTED)

    def text(self, x, y, value, size=21, color=INK, bold=False):
        value = LABELS.get(value, value)
        if LANGUAGE == 'en' and any('\uac00' <= char <= '\ud7a3' for char in value):
            raise ValueError(f'Missing English diagram label: {value}')
        self.parts.append(f'<text x="{x}" y="{y}" font-family="Apple SD Gothic Neo, Noto Sans CJK KR, sans-serif" font-size="{size}" font-weight="{700 if bold else 400}" fill="{color}">{escape(value)}</text>')

    def box(self, x, y, w, h, title, lines, color=BLUE, fill='#ffffff'):
        self.parts.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="14" fill="{fill}" stroke="#cbd7e4" stroke-width="1.5"/>')
        self.parts.append(f'<rect x="{x}" y="{y+16}" width="5" height="{h-32}" rx="2" fill="{color}"/>')
        self.text(x+22, y+37, title, 24, color, True)
        for i, line in enumerate(lines):
            self.text(x+22, y+70+i*29, line)

    def arrow(self, x1, y1, x2, y2, color=BLUE, dashed=False):
        dash = ' stroke-dasharray="7 6"' if dashed else ''
        self.parts.append(f'<path d="M{x1} {y1} L{x2} {y2}" stroke="{color}" stroke-width="2.5" fill="none"{dash}/>')
        if y2 > y1:
            points = f'{x2-6},{y2-10} {x2},{y2} {x2+6},{y2-10}'
        elif y2 < y1:
            points = f'{x2-6},{y2+10} {x2},{y2} {x2+6},{y2+10}'
        elif x2 > x1:
            points = f'{x2-10},{y2-6} {x2},{y2} {x2-10},{y2+6}'
        else:
            points = f'{x2+10},{y2-6} {x2},{y2} {x2+10},{y2+6}'
        self.parts.append(f'<polyline points="{points}" fill="none" stroke="{color}" stroke-width="2.5"/>')

    def save(self, name):
        if LANGUAGE == 'en':
            name = f'{Path(name).stem}.en.png'
        svg = ('\n'.join(self.parts) + '</svg>').encode()
        renderer = QSvgRenderer(QByteArray(svg))
        assert renderer.isValid()
        image = QImage(self.width*2, self.height*2, QImage.Format.Format_ARGB32)
        image.fill(0xffffffff)
        painter = QPainter(image)
        renderer.render(painter)
        painter.end()
        assert image.save(str(OUT / name))
        print(name, image.width(), image.height())


def overview():
    d = Diagram(880, 580, '레이더 추적 HIL · 시스템 구성', '센서 측정은 PC에서 생성하고, 추적 계산은 STM32에서 수행합니다.')
    d.box(36, 120, 808, 120, 'PC  /  pc_sim', ['합성 표적 · 센서 잡음 → 측정 프레임', 'GUI · CSV · 정답 / 측정 / 추정값 비교'], TEAL)
    d.arrow(180, 246, 180, 343)
    d.text(202, 277, '측정 · INIT / RESET · 상태 요청', 21, BLUE)
    # The return path is drawn separately to keep the two directions readable.
    d.arrow(700, 339, 700, 249, TEAL)
    d.text(202, 315, '추정 결과 TRACK · 오류 통계 STATUS', 21, TEAL)
    d.box(36, 350, 808, 120, 'STM32F746G-DISCO  /  Zephyr', ['UART 수신 → 예측 · 게이팅 · 칼만 보정 → 결과 송신', 'RX / 처리 / TX 스레드 · 단일 트랙 상태 관리'])
    d.text(36, 512, '데이터: USB-UART ↔ USART6  ·  115200 baud / 8N1', 21)
    d.text(36, 546, '진단: USART1 → ST-Link USB → 별도 콘솔', 21, MUTED)
    d.save('system-overview.png')


def pc_flow():
    d = Diagram(880, 1000, 'PC와 보드의 입출력', '정답 데이터는 PC에만 남고, 보드에는 센서 측정값을 전달합니다.')
    d.box(36, 118, 808, 115, '01  측정 생성  /  gui_model', ['표적 운동 + 센서 잡음 → 거리 · 방위각 · 센서 시각', '합성 정답은 GUI 비교 및 CSV 평가에만 사용'], TEAL)
    d.arrow(440, 238, 440, 269)
    d.box(36, 276, 808, 115, '02  송수신  /  SessionWorker', ['시리얼 포트 소유 · 프레임 인코딩 · 입력 송신', 'MEAS_REPORT · INIT · STATUS_REQUEST'], TEAL)
    d.arrow(200, 397, 200, 457)
    d.text(225, 431, 'USB-UART → USART6 RX', 21, BLUE)
    d.box(36, 464, 808, 115, '03  STM32  /  추적 처리', ['측정 해석 → 예측 → 게이팅 → 보정 · 트랙 상태 전이', 'TRACK_REPORT: 추정 결과  /  STATUS_REPORT: 누적 통계'])
    d.arrow(200, 585, 200, 645, TEAL)
    d.text(225, 619, 'USART6 TX → USB-UART → SessionWorker', 21, TEAL)
    d.box(36, 652, 808, 115, '04  수신 해석  /  FrameParser + protocol', ['프레임 조립 · LEN / CRC 확인 · STATUS / TRACK 해석', 'GUI로 전달 → 보드 추정값 · 상태 · 오류 통계 표시'], TEAL)
    d.arrow(440, 773, 440, 804, TEAL)
    d.box(36, 811, 808, 115, '05  기록과 평가  /  SessionRecorder', ['송수신 · 측정 · 보고 CSV 기록 → 시각 정렬 → RMSE', '기록 데이터는 송수신 워커에서 수집'], TEAL)
    d.text(36, 969, '별도 경로: 보드 USART1 → ST-Link → GUI 콘솔 / 터미널', 21, MUTED)
    d.save('pc-board-flow.png')


def firmware_flow():
    d = Diagram(880, 1400, '펌웨어 · ISR에서 결과 송신까지', '실선: 데이터 이동     점선: 알림     큐: 구조체 복사')
    d.box(36, 118, 690, 120, '01  UART ISR  /  uart_callback', ['USART6 RX → uart_fifo_read → ring_push', 'ISR에서는 바이트 저장 · RX 알림만 수행'])
    d.arrow(365, 244, 365, 275)
    d.box(36, 282, 690, 88, 'RX 링버퍼  /  rx_ring', ['256바이트 배열 · 실제 저장 용량 255바이트'], TEAL, '#eaf6f5')
    d.arrow(365, 376, 365, 407)
    d.box(36, 414, 690, 155, '02  RX 스레드  /  uart_rx_thread', ['ring_pop · overflow 복구 · 부분 프레임 timeout', 'rx_frame_processor → frame_parser', 'LEN / CRC 검사 → 프레임 조립'])
    d.parts.append(f'<path d="M732 180 H850 V480" fill="none" stroke="{MUTED}" stroke-width="2.5" stroke-dasharray="7 6"/>')
    d.arrow(850, 480, 732, 480, MUTED, True)
    d.text(744, 278, 'rx_sem', 19, MUTED, True)
    d.text(744, 310, '알림만', 19, MUTED)
    d.text(744, 342, '50ms', 19, MUTED)
    d.text(744, 370, '대기 상한', 17, MUTED)
    d.arrow(365, 575, 365, 606)
    d.box(36, 613, 808, 88, 'frame_queue  /  8개', ['frame_handler_submit · 프레임 + 수신 완료 시각 복사'], TEAL, '#eaf6f5')
    d.arrow(440, 707, 440, 738)
    d.box(36, 745, 808, 185, '03  처리 스레드  /  frame_handler_thread', ['frame_handler_process → TYPE 분기 · INIT / RESET', 'radar_processor → 측정 해석 · 예측 · 게이팅 · 보정', 'report_sender → TRACK / STATUS payload 구성', '무수신 감시 · RX 오류 통계 조회 · 오래된 측정 폐기'])
    d.arrow(440, 936, 440, 967)
    d.box(36, 974, 808, 88, 'tx_queue  /  8개', ['uart_tx_submit · 보고서 프레임 복사'], TEAL, '#eaf6f5')
    d.arrow(440, 1068, 440, 1099)
    d.box(36, 1106, 808, 145, '04  TX 스레드  /  uart_tx_thread', ['protocol_frame_encode → STX / LEN / TYPE / payload / CRC', 'uart_poll_out → 프레임 바이트 순서대로 송신', 'USART6 TX → PC'])
    d.text(36, 1295, '트랙과 보고 순번의 소유자: 처리 스레드', 21)
    d.text(36, 1331, 'RX / 처리 스레드는 입력이 없어도 주기적으로 timeout 확인', 21, MUTED)
    d.text(36, 1367, '현재 TX는 polling 방식 · 큐 접수 성공은 상대 수신 ACK가 아님', 21, MUTED)
    d.save('firmware-flow.png')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--lang', choices=('ko', 'en'), default='ko')
    args = parser.parse_args()
    LANGUAGE = args.lang
    if LANGUAGE == 'en':
        LABELS = json.loads((Path(__file__).with_name('diagram-labels.en.json')).read_text(encoding='utf-8'))
    app = QApplication.instance() or QApplication([])
    OUT.mkdir(parents=True, exist_ok=True)
    overview()
    pc_flow()
    firmware_flow()
