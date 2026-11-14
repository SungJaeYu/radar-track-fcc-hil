# 실보드 로그 포화 시험

일반 펌웨어와 분리된 일회성 bench fixture입니다. `CONFIG_RADAR_LOG_STRESS_TEST`는
기본 비활성이며 ASSERT와 deferred logger가 필요합니다. 생산 경로의 RX/handler/TX 코드는 변경하지 않습니다.

- `log_stress.c`: 부팅 8초 뒤 2,000개씩 10회 생성, burst 사이 100ms 양보,
  마지막 2초 뒤 완료 표시. 우선순위 10으로 RX(5)/handler(6)/TX(7)보다 낮습니다.
- `stress.conf`: 시험 옵션만 활성화합니다. 4KiB 로그 버퍼·비차단·새 로그 폐기 정책은 유지합니다.
- `board_check.py`: 콘솔 수집을 먼저 시작하고 플래시한 뒤 즉시 측정·STATUS를 송수신합니다.
  두 UART를 독점하며 GUI/다른 송신기를 먼저 종료해야 합니다. 기존 결과 폴더는 덮어쓰지 않습니다.

## 재현

아래 `REPO`, `ZEPHYR_WS`, 포트는 로컬 경로에 맞게 지정합니다. pyserial이 필요합니다.

```sh
REPO=/path/to/radar-track-fcc-hil
ZEPHYR_WS=/path/to/zephyrproject
cd "$ZEPHYR_WS"
"$ZEPHYR_WS/.venv/bin/west" build -d "$REPO/build_log_stress" \
  -b stm32f746g_disco "$REPO/fcc_app" -- \
  '-DEXTRA_CONF_FILE=debug.conf;tests/log_stress/stress.conf' \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cd "$REPO"
"$ZEPHYR_WS/.venv/bin/python" fcc_app/tests/log_stress/board_check.py \
  --mode producer --build-dir build_log_stress --output-dir /tmp/log-stress-producer \
  --data-port DATA_PORT --console-port CONSOLE_PORT \
  --west "$ZEPHYR_WS/.venv/bin/west" --zephyr-workspace "$ZEPHYR_WS"
```

실제 측정 부하는 별도 디버그 빌드(`-DEXTRA_CONF_FILE=debug.conf`)로 만들고
`--mode traffic --build-dir build_logging_debug --output-dir /tmp/log-stress-traffic`로 실행합니다.
producer는 10Hz 측정과 병행하며, traffic은 목표 10→50→100→200→10Hz입니다.
STATUS의 동기 대기로 실제 평균 송신률은 낮아집니다. `sent / duration_s`를 실측값으로 보고하며
200Hz 지속 송신이나 최대 처리율 시험으로 해석하지 않습니다. 늦어진 송신을 burst로 보충하지 않습니다.

## 판정과 한계

로그 backend의 `messages dropped` 알림, 모든 STATUS 통계 0, 각 구간·종료 후 응답,
응답 CRC/길이, 단일 부팅, CONFIRMED 전이, 마지막 측정 로그와 1회 timeout을 확인합니다.
producer는 시작·완료 표시도 확인합니다. 실패해도 수집된 콘솔·wire·판정을 보관합니다.
로그 드롭 수는 시험 로그와 애플리케이션 로그를 모두 포함하므로 sequence 누락 수와 다릅니다.
STATUS 대기 2초는 시험 제한이며 반환 시간에는 PC read 대기(최대 50ms)가 포함됩니다.

이 시험은 모든 측정의 처리 완료/추정 정확도, 스택 여유, 하드웨어 UART 오류,
장시간 안정성, 반복 WARN 공격을 증명하지 않습니다. 자동 전원복구 시험도 아닙니다.

**시험 종료 또는 실패 후 기본 `build`를 재빌드·플래시하고 STATUS를 확인해야 합니다.**
스크립트는 결과 보존을 위해 복원 펌웨어를 임의로 선택하지 않습니다.
기본/일반 DEBUG `.config`의 시험 옵션 비활성과 `compile_commands.json`의 시험 소스 제외도 확인합니다.

[2026-11-14 결과](../../../docs/hardware-test-2026-11-14-log-stress.md)
