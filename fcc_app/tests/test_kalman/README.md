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

실행 파일 `track_tests`를 직접 실행하면 17개 개별 테스트 이름을 볼 수 있습니다.
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


## 공통 host 실행 형식

`track_tests`를 인자 없이 실행하면 전체 사례를 실행합니다.
`track_tests test_name`으로 한 사례만 선택할 수 있습니다.
각 성공은 `PASS: test_name`으로 출력하며 잘못된 이름은 종료 코드 2를 반환합니다.
실행기는 `../common/test_runner.h`를 공유합니다. 모사 환경과 검증 범위는 각 suite가 유지합니다.

## 각도 정규화·비정상 공분산 회귀

- `test_angle_wrap_boundaries`: 0, ±π, ±2π와 π 양옆의 인접 float.
- `test_angle_wrap_multiple_turns`: ±10바퀴와 양·음 잔여 각도.
- `test_angle_wrap_large_finite_values`: ±1e10, ±FLT_MAX의 종료·출력 범위·재정규화 안정성. 큰 입력의 물리적 각도 정확도를 보장하지는 않습니다.
- `test_correction_rejects_invalid_innovation_covariance`: s=0, 음수, 임계값 이하, NaN, ±Inf에서 선형·각도 보정이 필터를 변경하지 않음.

CTest에 10초 제한을 두어 반복문으로 되돌아갈 때 무한 대기를 검출합니다.
NaN·±Inf 각도 입력의 assert 실패는 `tests/test_firmware_assertions.py`에서 자식 프로세스로 검증합니다.

## 게이트 설정 행렬

host 모드는 기존 `track_tests` 17개와 아래 20개를 실행합니다. CTest 등록 항목은 총 21개입니다.
`gating_cases.c`가 실제 `gating.c`·`angle_utils.c`를 설정별 실행 파일로 빌드합니다.
Zephyr Ztest는 기존 17개를 기본 설정으로 실행하며 host 전용 행렬은 포함하지 않습니다.

| 실행 파일 | 거리 half-width (m) | 각도 half-width (°) |
|---|---:|---:|
| `gating_minimum` | 1 | 1 |
| `gating_narrow` | 50 | 5 |
| `gating_default` | 100 | 10 |
| `gating_wide` | 250 | 30 |
| `gating_maximum` | 65535 | 180 |

각 설정은 다음 네 사례를 실행합니다. 요구사항은 REQ-TRK-002 / SPEC-TRACK입니다.

| 함수 | 검증 |
|---|---|
| `range_boundary` | 양·음 경계 및 내부 허용, 경계 밖 거부 |
| `angle_boundary` | 양·음 경계와 인접 float, 180° 바깥 값의 wrap 후 허용 |
| `both_required` | 두 조건 모두 충족해야 통과, 한쪽/양쪽 초과 거부 |
| `wrapped_error` | ±π 횡단의 최단 각도 차이, 두 끝점 동일 방향, wrap 이후에도 한계 밖이면 거부 |

`ctest --test-dir build_tracking -R gating_ --output-on-failure`로 행렬만 실행할 수 있습니다.
180°에서는 각도 초과 거부 사례가 성립하지 않아 해당 분기를 제외하고 거리 거부를 확인합니다.
최대 거리 설정 시험은 게이트 함수의 합성 float 입력이며 uint16 wire 측정 범위 검사가 아닙니다.
테스트 π 리터럴은 생산 상수와 독립적으로 유지합니다.

2026-11-14: STM32용 Zephyr Ztest 앱도 빌드 성공했으며 기본 gate 설정이 생성된 `.config`에 반영됨을 확인했습니다. 이 앱의 보드 실행은 하지 않았습니다.
