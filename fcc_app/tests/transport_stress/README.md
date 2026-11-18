# UART consumer 정지 기반 실보드 포화 시험

일반 처리 코드를 변경하지 않고 시험용 스레드가 부팅8초 뒤 RX·handler·TX를 차례로4초씩 정지합니다. 각 단계 뒤4초 복구 구간을 둡니다. IRQ는 유지합니다. 일반/디버그 구성에는 기본적으로 포함되지 않습니다.

- RX 정지: PC가600바이트를 보내255바이트 링을 넘기고 overflow·ISR 폐기를 확인합니다.
- handler 정지: 측정24개를10ms 간격으로 보내 RX 큐 포화와 재개 시 오래된 측정 폐기를 확인합니다.
- TX 정지: STATUS24개를 보내 큐 포화 WARN과 재개 후 보존된 응답을 확인합니다. 큐8개에 정지 직전/대기 중 직접 전달된 항목1개가 더 있을 수 있어8~9개를 허용합니다.
- 각 단계 뒤 INIT·정상 측정3개로 CONFIRMED와 RESET 복귀를 확인합니다.

```sh
# Zephyr workspace에서 REPO를 실제 저장소 경로로 변경
west build -d "$REPO/build_transport_stress" -b stm32f746g_disco "$REPO/fcc_app" -- -DEXTRA_CONF_FILE=tests/transport_stress/stress.conf
# 복원할 일반 build도 최신 상태로 먼저 준비
west build -d "$REPO/build" -b stm32f746g_disco "$REPO/fcc_app"
cd "$REPO"
python fcc_app/tests/transport_stress/board_check.py --data-port DATA --console-port CONSOLE --workspace /path/to/zephyrproject --output /tmp/new-transport-run
```

기존 폴더는 덮어쓰지 않습니다. CLI의 --west로 실행파일 경로를 지정할 수 있습니다.
스크립트는 시험 이미지를 flash하고 실제 두 포트를 사용합니다. 성공/실패 후 일반 build를 다시 flash하고 STATUS0을 확인합니다. 강제 종료·전원 분리 시 finally 복원을 보장할 수 없으므로 수동 복원이 필요합니다.

실제 IRQ·링·큐·UART 경로를 사용하지만 스레드 정지는 인위적인 고장 주입입니다. 정상 부하에서 자연 발생하는 포화 빈도·최대 처리량·전체 동시성·UART 하드웨어 오류를 증명하지 않습니다. 이 시험은 기본 DRAIN만 대상으로 합니다. 제어 요청 우선 보존 정책을 추가하지 않습니다.

## TRACK·제어 요청 추가 모드

같은 명령에 `--track-controls`를 붙이면 TX 단계에 STATUS 대신 측정24개를 전송합니다.
접수된 TRACK의 입력 시각·보고 순번 FIFO 순서, 거부된 보고에 해당하는 순번 공백과 다음 INIT/정상 측정 복귀를 검사합니다. TX 정지 동안 무수신 timeout 보고도 한 번 거부될 수 있어 순번 공백 판정에 포함합니다.

handler 정지 단계에서는 큐를 채운 뒤 INIT과 RESET을 추가 전송합니다. 측정16개와 제어2개의 queue_drops, 보존된 측정8개의 stale 폐기, 제어 보고 부재를 함께 판정합니다. 재개 후 정상 구간에서는 측정→RESET→측정→INIT→측정의 보고 순서와 새 추정값을 확인합니다.
이는 현재 FIFO·새 요청 거부 정책의 관측이며 제어 요청을 보장하는 정책을 추가하지 않습니다. INIT/RESET은 큐 포화 시 버려질 수 있고 UART write 성공은 명령 처리 확인이 아닙니다. 트래픽을 낮춘 뒤 명시적으로 재초기화하고 새 측정의 STARTED/CONFIRMED를 확인해야 합니다. 세션 ID/전용 ACK가 없어 모든 실행에서 명령 응답을 엄격히 대응하는 것은 불가능합니다.
