# 문서 이미지 갱신

README와 EXPLAINER의 다이어그램은 GitHub 앱에서도 별도 Mermaid 렌더러 없이 표시하도록 PNG로 배포합니다. 그림의 원본은 `render_diagrams.py`이며, 시스템 구성·PC 왕복·펌웨어 내부 흐름을 각각 생성합니다.

저장소 루트에서 GUI 의존성이 설치된 Python 환경으로 실행합니다.

```sh
python -m pip install -r pc_sim/requirements-gui.txt
python tools/docs/render_diagrams.py
python tools/docs/render_diagrams.py --lang en
python tools/docs/capture_gui.py
```

- 다이어그램: `docs/images/system-overview.png`, `pc-board-flow.png`, `firmware-flow.png`.
- 영문 다이어그램: 같은 이름의 `.en.png` 파일. `--lang en`은 `diagram-labels.en.json`의 영문 라벨을 사용하고 한국어 PNG를 덮어쓰지 않습니다. 라벨을 수정할 때 해당 언어의 PNG도 다시 생성합니다.
- GUI 캡처: `docs/images/radar-gui-preview.png`, `radar-gui-track-preview.png`.
- 캡처는 실제 `MainWindow`와 `SessionWorker`를 Qt offscreen으로 실행합니다. 1440×900 화면을 2배 해상도로 저장합니다.
- 미리보기 설정: 초기 거리1000m, 변화율−10m/s, 방위각15°, 거리 잡음σ5m, 10Hz. 약11초간 생성 후 중지하고 설정·TRACK 탭을 캡처합니다. 호스트 실행 시각에 따라 표본 시각은 달라질 수 있습니다.
- UART 포트를 열거나 보드 응답을 주입하지 않습니다. 보드 추정값이 비어 있는지 확인합니다. 저장 경로 표시는 저장소 기준 `runs/`로 설정하고 CSV 기록은 끈 상태입니다.

생성 후 글자·선 겹침, 잘림, 실제 구현과 설명의 일치를 확인합니다. 문서만 갱신하는 작업이며 실보드 검증을 대신하지 않습니다. 날짜별 시험 문서의 과거 GUI 캡처는 덮어쓰지 않습니다.
