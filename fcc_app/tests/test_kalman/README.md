# Tracking C regression tests

현재 `kalman_cv`, `angle_utils`, `gating`, `track`, `radar_message` 구현을 직접
컴파일합니다. 예전 Cartesian `kalman_state` 테스트를 대체합니다.

검증 범위:
- semicircle 수신 변환, radian wrap 및 gating
- CV 예측·보정, 공분산 계산, 정지 표적 수렴
- timestamp 중복·역행·순환, 긴 공백 이후 재시작
- TENTATIVE / CONFIRMED / COASTING / LOST 전환
- LOST에서 timestamp가 재시작되는 경우와 비활성 상태 호출

## 호스트 실행 (보드 불필요)

저장소 루트에서:

```sh
cmake -G Ninja -S fcc_app/tests/test_kalman -B /tmp/radar-track-tests -DTRACK_TEST_HOST=ON
cmake --build /tmp/radar-track-tests
ctest --test-dir /tmp/radar-track-tests --output-on-failure
```

실행 파일 `track_tests`를 직접 실행하면 13개 개별 테스트 이름을 볼 수 있습니다.
호스트 실행은 Zephyr 스레드, UART, 보드 동작을 검증하지 않습니다.
링버퍼 크기·overflow drain 복구는 별도의 [UART host 테스트](../test_uart_rx/README.md)에서 검증합니다.
해당 테스트 역시 실제 보드 IRQ 타이밍이나 하드웨어 수신 성공을 보장하지 않습니다.

## Zephyr 실행

`native_sim`을 지원하는 Zephyr 환경에서:

```sh
west build -b native_sim fcc_app/tests/test_kalman -d /tmp/radar-track-ztest
west build -d /tmp/radar-track-ztest -t run
```

동일한 테스트 본문을 Zephyr Ztest로 등록합니다. macOS 호스트 테스트 통과와
Zephyr native_sim 또는 STM32 보드 실행 결과는 별도로 구분해야 합니다.
