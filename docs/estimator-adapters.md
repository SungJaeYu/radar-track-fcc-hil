# 추적 알고리즘 교체 구조

`radar-tracking-bench`는 합성 레이더 항적을 입력하고 STM32에서 추정 정확도와
실행 비용을 비교하는 평가 플랫폼으로 발전시킨다. 현재 단계는 기존 단일표적
HIL의 동작을 보존하면서 추정기 교체 경계를 만드는 것이다.

기준 태그는 `v1-hil-baseline` (`af406a1`), 작업 브랜치는
`feat/tracking-benchmark`다. 기존 `v1.0.0` 태그도 보존한다.

## 책임과 API

```text
radar_processor → track → estimator.h → 선택한 어댑터 → 알고리즘
                       ↘ 공통 출력 → 게이팅 / report_sender
```

- `track.c`: timestamp 검사, 초기화·예측·보정 호출, hit/miss와 트랙 상태 전이.
- `estimator.h`: 초기화·예측·보정·출력·구현 이름의 공통 계약.
- `estimators/polar_cv.c`: 기존 거리/방위각 KF 두 개를 감싸는 기본 어댑터.
  기존 튜닝 값, 호출 순서, 각도 wrap, 수학 구현은 유지한다.
- `radar_processor.c`, `report_sender.c`: `estimator_output`만 읽고 내부 필터 필드에 접근하지 않는다.

| API | 입력 또는 결과 |
|---|---|
| `estimator_name()` | 빌드에 포함된 구현 이름. 부팅 콘솔에도 출력 |
| `estimator_init()` | 최초 거리 m, 방위각 rad; 모든 내부 상태 재초기화 |
| `estimator_predict()` | 경과 시간 s; 트랙 계층에서 timestamp 검사를 마친 값 |
| `estimator_correct()` | 게이트를 통과한 거리 m, 방위각 rad |
| `estimator_get_output()` | 거리 m, 방위각 rad, 거리 변화율 m/s, 방위 변화율 rad/s |

추정기 객체는 각 트랙이 정적으로 소유한다. 동적 할당과 전역 가변 상태는 없다.
내부 상태의 구조체 정의는 정적 메모리 할당을 위해 헤더에 보이지만, 호출자는
그 필드에 의존하지 않는다. 수치 단위 테스트만 기본 KF 내부를 직접 확인한다.
트랙 reset은 기존처럼 추정기를 읽거나 지우지 않으며, 다음 start가 재초기화한다.
보고의 유효성은 트랙 상태가 결정하고 EMPTY/LOST 보고는 추정치를 읽지 않는다.

## 빌드 시 선택

현재 제공하는 실제 알고리즘은 **`polar_cv` 하나**다. 아래 옵션은 실행 중 교체가
아니라 빌드할 어댑터를 선택한다. host와 Zephyr 모두 `fcc_app/cmake/estimator.cmake`를
사용하며, 알 수 없는 이름은 구성 단계에서 실패한다.

```sh
# 저장소 루트에서 host 회귀 검사
cmake -G Ninja -S fcc_app/tests/test_tracking -B /tmp/radar-bench-tracking \
  -DTRACK_TEST_HOST=ON -DRADAR_ESTIMATOR=polar_cv
cmake --build /tmp/radar-bench-tracking
ctest --test-dir /tmp/radar-bench-tracking --output-on-failure

# Zephyr 환경에서 <repo>를 실제 체크아웃 절대 경로로 치환
west build -d <repo>/build_polar_cv -b stm32f746g_disco <repo>/fcc_app -- \
  -DRADAR_ESTIMATOR=polar_cv
```

선택 결과는 `RADAR_ESTIMATOR_HEADER` 전처리기 정의로 타깃 전체에 전달되고,
해당 구현의 소스만 링크한다. 함수 포인터 디스패치 비용은 없다.
GitHub 저장소 이름 변경은 기존 로컬 디렉터리 이름을 변경하지 않는다.

## 다음 알고리즘을 추가하는 방법

1. `estimators/<name>.h`에 해당 구현의 `struct estimator` 저장 공간을 정의한다.
2. `estimators/<name>.c`에서 다섯 공통 API를 구현한다. 내부 상태가 직교좌표여도
   반환값은 공통 관측 좌표와 단위로 변환한다. 영거리 등 변환의 특이점 처리도
   새 어댑터의 계약과 테스트에 포함한다.
3. `cmake/estimator.cmake`에 이름, 헤더 정의, 필요한 소스를 함께 등록한다.
4. 해당 어댑터의 수치 검증을 추가하고 공통 lifecycle·프로토콜 검증을 수행한다.
   기존 `test_tracking`에는 polar KF 전용 white-box 사례가 있어 그대로 다른
   알고리즘의 정확도 시험으로 사용하지 않는다.
5. 별도 빌드 폴더에서 같은 기록 입력을 재생한다. 알고리즘 이름·튜닝·빌드 설정을
   결과 metadata에 남기는 평가 도구 확장은 후속 작업이다.

`estimator_swap` host 검사는 KF와 다른 저장 구조를 가진 시험용 어댑터를 실제
`track.c`와 링크한다. KF를 링크하지 않고도 트랙 생성, 두 인스턴스 독립성,
시간 거부, 예측·보정, coasting/loss 및 재시작이 동작하는지 확인한다.
이 시험용 구현은 펌웨어에서 선택할 수 있는 비교 알고리즘이 아니다.

## 범위와 검증

측정/보고 wire 규격, 게이트, timeout, 트랙 상태 정책은 유지한다. 새 기동 시나리오,
EKF/UKF/IMM, cycle 단위 계측, 알고리즘 A/B 비교 화면은 아직 추가하지 않았다.
기존 RTT는 통신과 호스트 대기를 포함하므로 필터 연산 시간으로 사용하지 않는다.

2026-11-20 검증 결과:

- 추적·게이트·교체 host CTest 22개와 처리·보고 host CTest 31개 통과.
  `tracking` 항목 내부의 여러 수치 사례는 CTest 항목 개수와 구분한다.
- Python 765개 및 subtest 9개 통과.
- 등록하지 않은 backend 이름의 CMake 구성 실패 확인.
- STM32F746G-DISCO Zephyr 일반 빌드 성공: FLASH 42,976 B / RAM 15,696 B.
  devicetree 중복 unit-address 경고와 Xcode 라이선스로 인한 Zephyr 버전용
  `git describe` 실패 메시지가 있었으며 최종 링크·HEX 생성은 성공했다.

기존 실보드 시험 기록은 당시 버전의 증거다. 이번 어댑터 변경을 플래시하거나
HIL로 재시험한 것은 아니다.
