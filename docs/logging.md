# 펌웨어 로깅

애플리케이션 로그는 Zephyr deferred logging을 사용한다. RX/handler/TX는 메시지를
로그 버퍼에 넣고 Zephyr 로깅 스레드가 ST-Link 콘솔 UART로 출력한다.
USART6의 이진 프로토콜과 STATUS_REPORT 규격은 변경하지 않는다.

## 모듈과 레벨

| 소스 | 모듈 이름 | Kconfig 접두사 |
|---|---|---|
| main.c | radar_main | RADAR_MAIN |
| transport/uart_rx.c | uart_rx | RADAR_UART_RX |
| transport/rx_frame_processor.c | rx_frame_processor | RADAR_RX_PROCESSOR |
| application/frame_handler.c | frame_handler | RADAR_HANDLER |
| transport/uart_tx.c | uart_tx | RADAR_UART_TX |

각 파일에서 `LOG_MODULE_REGISTER(name, CONFIG_RADAR_..._LOG_LEVEL)`로 등록한다.
일반 빌드는 다섯 모듈 모두 INFO, debug.conf는 DEBUG다.
ERR는 초기화·인코딩·예상 밖 제출 실패, WRN은 overflow·큐 드롭·무수신·잘못된 외부 입력,
INF는 부팅/준비 완료·INIT/RESET·트랙 생성/상태 변화, DBG는 측정·예측·처리 결과와 STATUS 큐 접수다.
heartbeat와 TYPE만 반복하는 로그는 제거했다. 정상 측정 상세는 입력·예측·결과 3줄로 묶고 timestamp와 단위를 표시한다.
STATUS 통계는 wire 응답으로 조회하며 콘솔에 중복 출력하지 않는다. 큐 접수는 실제 송신 완료를 뜻하지 않는다.
미구현 COMMAND와 잘못된 입력은 무시한 이유를 남기고, STATUS 큐 포화는 WARN, 예상 밖 실패는 ERR로 구분한다.
초기화 오류는 UART 소유 모듈에서 한 번 기록한다. overflow 복구 경고에는 누적 이벤트·폐기 바이트를 포함한다.
게이트 탈락과 중복/역행 측정 시각은 DEBUG로 남긴다.
큐 드롭 경고는 기존대로 첫 발생과 100회마다 출력한다. 다른 반복 경고의 빈도 제한은 후속 과제다.

로그 메시지에 끝 개행을 붙이지 않는다. 서로 다른 LOG 호출은 별개 메시지이므로
여러 줄을 하나의 트랜잭션처럼 묶는 보장은 없다. 계산 모듈에는 새 로그 의존성을 추가하지 않는다.

## 설정 파일의 역할

- `fcc_app/Kconfig`: 항목·선택지·기본값·의존성을 정의한다. Zephyr의
  `subsys/logging/Kconfig.template.log_config`를 모듈별로 포함한다.
- `module`: 생성할 심볼 접두사, `module-str`: 설정 화면에 표시할 설명이다.
- `fcc_app/prj.conf`: 일반 빌드의 값을 선택한다.
- `fcc_app/debug.conf`: `-DEXTRA_CONF_FILE=debug.conf`로 지정하면 일반 설정 위에 적용한다.
- 최종 값은 빌드 디렉터리의 `zephyr/.config`와 생성된 `autoconf.h`에 저장된다.
  생성 파일을 직접 수정하지 않는다. 설정 변경 후 다시 빌드한다.

예: `CONFIG_RADAR_HANDLER_LOG_LEVEL_INF=y`를 선택하면 파생 숫자
`CONFIG_RADAR_HANDLER_LOG_LEVEL=3`이 만들어진다. DEBUG 선택은 4다.
OFF=0, ERR=1, WRN=2, INF=3, DBG=4이며 선택한 수준보다 심각한 로그도 포함한다.
숫자 심볼은 직접 지정하지 않고 템플릿의 선택지를 사용한다.

## 공통 옵션

| 옵션 | 값 | 의미 |
|---|---|---|
| CONFIG_LOG | y | 로깅 활성화 |
| CONFIG_LOG_MODE_DEFERRED | y | 메시지 처리를 뒤로 미룸 |
| CONFIG_LOG_PROCESS_THREAD | y | Zephyr 로깅 스레드 생성 |
| CONFIG_LOG_BACKEND_UART | y | 현재 console UART로 출력 |
| CONFIG_LOG_PRINTK | y | Zephyr 등의 printk도 로깅 경로로 전달; 모듈 레벨 필터는 적용되지 않음 |
| CONFIG_LOG_BUFFER_SIZE | 4096 | 메시지 버퍼 바이트 수; 스택·RX 링과 별개 |
| CONFIG_LOG_BLOCK_IN_THREAD | n | 버퍼 공간을 기다리며 애플리케이션 스레드를 정지하지 않음 |
| CONFIG_LOG_MODE_OVERFLOW | n | 공간 부족 시 오래된 로그 대신 새 로그 폐기 |

로그 버퍼 폐기는 RX 데이터 손실과 다른 사건이다. 현재 STATUS 통계에는 로그 유실이 포함되지 않는다.
4096바이트는 초기 설정이며 무손실 출력이나 충분한 스택 여유를 보장하지 않는다.
panic/fatal 처리에서는 정상 deferred 경로와 동작이 다를 수 있다.

## 빌드와 확인

Zephyr workspace에서 실제 저장소 경로를 `<repo>`에 넣는다.

```sh
west build -d <repo>/build -b stm32f746g_disco <repo>/fcc_app
west build -d <repo>/build_logging_debug -b stm32f746g_disco <repo>/fcc_app -- -DEXTRA_CONF_FILE=debug.conf
```

디버그 설정은 assert도 활성화한다. 파일 이름만으로 컴파일러 최적화 수준을 변경하지는 않는다.
일반 빌드에서는 측정 상세·STATUS 큐 접수 텍스트가 보이지 않는 것이 정상이다. heartbeat는 출력하지 않는다.
상세 로그가 필요한 보드 시험은 디버그 빌드를 사용하고, STATUS 값은 CLI/GUI wire 응답으로 확인한다.
과거 보드 시험 스크립트는 당시 출력과 펌웨어의 증거용 스냅샷이다. 최신 펌웨어에서 재사용할 때
레벨, timestamp/모듈 접두사, 지연 출력에 맞게 판정과 대기 조건을 조정한다.

## 검증과 남은 범위

- host logging 헤더는 한 LOG 호출을 기존 printk 캡처 한 번으로 연결한다.
  필터링·실제 큐·스케줄링·포화·출력 직렬화는 모사하지 않는다.
- RX/handler/TX/Tracking host 회귀 및 Python 시험으로 기존 처리·오류 로그 경로를 확인한다.
- 일반/디버그 빌드의 최종 모듈 레벨과 ELF의 상세 로그 문자열 포함 여부를 확인한다.
- [기본 실보드 회귀](hardware-test-2026-11-14-logging.md)는 일반/디버그 각 30조건 통과했다. 관측한 문장의 형식과 UART 왕복을 확인했다.
- 강제 로그 포화·유실 알림 발생, 반복 경고 폭주, 최악 RX 처리 영향·스택 실측은 후속 검증이다.

2026-11-14 전환: C host 103회, Python 160개 + subtest 9개 통과.
일반·디버그 펌웨어 빌드와 레벨/문자열 검증은 완료했으며 이번 전환의 flash·실보드 시험은 수행하지 않았다.

후속 로그 정리: handler 17개·RX 45개·TX 4개 통과. 기존 사례에서 정상 측정 3줄,
상태 전이 출력, STATUS 접수 한 줄, 실패 시 접수 로그 없음과 큐 포화 문구를 확인했다.
일반·디버그 빌드 통과. 실보드 로그·포화 검증은 여전히 별도다.

후속 실보드 시험: `67d247b` 일반/디버그 각각 30조건 통과, 일반 INFO로 복원 후 STATUS 확인.
앞선 미실시 문단은 당시 기록이며 [실보드 증거](hardware-test-2026-11-14-logging.md)가 기본 회귀 상태를 갱신한다.
