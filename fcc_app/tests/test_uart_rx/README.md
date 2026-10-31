# UART drain 복구 host 회귀 테스트

실제 `uart_rx.c`의 ISR·복구 함수와 `ring_buffer.c`, `frame_parser.c`를 컴파일합니다.
임시 `/private/tmp` 테스트를 저장소로 옮긴 것으로, 테스트는 외부 임시 파일에 의존하지 않습니다.

## 실행

저장소 루트에서 C11 컴파일러·CMake·Ninja를 사용합니다.

```sh
cmake -G Ninja -S fcc_app/tests/test_uart_rx -B /tmp/radar-uart-tests
cmake --build /tmp/radar-uart-tests
ctest --test-dir /tmp/radar-uart-tests --output-on-failure
```

9개 사례를 CTest에 각각 등록했습니다. 실행 파일 `uart_rx_tests`를 직접 실행하면 전체를,
`uart_rx_tests test_receives_after_recovery`처럼 이름을 넘기면 한 사례를 실행합니다.
Release 빌드에서도 검증용 `assert`가 제거되지 않도록 `-UNDEBUG`를 적용합니다.

macOS에서 컴파일러 탐색 또는 `library 'System' not found` 오류가 나면,
설치된 Xcode 경로에 맞춰 컴파일러와 SDK를 명시할 수 있습니다.

```sh
cmake -G Ninja -S fcc_app/tests/test_uart_rx -B /tmp/radar-uart-tests \
  -DCMAKE_C_COMPILER=/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang \
  -DCMAKE_OSX_SYSROOT=/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk
```

`python -m pytest`와 별도 실행하는 C 테스트입니다. Zephyr SDK나 보드 연결은 필요하지 않습니다.

## 검증 범위

1. ISR에서 overflow를 일으킨 뒤 선택된 정책이 버퍼 내 정상 INIT 프레임을 처리하는지.
2. 기존 파서 상태와 버퍼의 뒷부분으로 레이더 프레임을 완성하는지.
   handler 호출 중 새 UART 입력을 동기적으로 주입해 저장되지 않는지도 확인합니다.
3. 불완전한 프레임을 처리하지 않고 복구 마지막에 폐기하는지.
4. 잘못된 체크섬을 가진 프레임을 거부하는지.
5. 복구 후 정상 프레임을 다시 수신·파싱할 수 있는지.
6. 빈 버퍼에서도 복구 후 overflow 해제·파서 초기화가 이루어지는지.
7. 꽉 찬 버퍼의 push 실패가 기존 바이트와 head/tail을 변경하지 않는지.
8. 초기화 없이 채우기·비우기를 반복해 인덱스가 배열 끝을 순환해도 바이트 순서와 full/empty 판정이 유지되는지.
9. drain 중 잘못된 길이를 거부하고 이어지는 정상 INIT만 처리하는지.

현재 `RX_OVERFLOW_DRAIN_BUFFER` 설정의 동작을 검증합니다. 전체 폐기로 설정을 바꾸면
정상 프레임을 보존해야 한다는 drain 검증은 실패합니다. 두 정책을 모두 자동 선택해 빌드하는 테스트는 아닙니다.

현재 `BUFFER_SIZE=256`이며 실제 저장 용량은 255바이트입니다.
fixture는 `BUFFER_SIZE - 1`에서 입력 길이와 프레임 절단 위치를 계산합니다.
WAIT_STX에서 무시되는 0으로 채운 뒤 용량 다음 바이트로 overflow를 발생시킵니다.
부분 프레임 폐기 사례는 프레임 앞부분을 버퍼 끝에 배치하여 패딩으로 완성되지 않게 합니다.
static assertion은 fixture의 최소 크기(9)와 현재 `uint8_t` 인덱스의 최대 크기(256)를 검사합니다.
인덱스 타입을 바꾸면 이 상한 검사도 함께 갱신해야 합니다.

모든 fixture는 CRC-16 기준이며 결과 비교는 `PARSER_FRAME_READY`를 명시합니다.

## 실제 코드와 모사의 경계

- 실제 코드: `uart_callback`, 복구 dispatcher·drain·마무리 함수, 링버퍼, 프레임 파서.
- 모사: UART FIFO, Zephyr 세마포어·IRQ·atomic·시각, 프레임 handler.
- `uart_rx_thread()`는 컴파일하지만 무한 루프를 실제로 스케줄링하지 않습니다.
- `uart_rx.c`를 테스트 번역 단위에 직접 포함해 static 함수를 검사합니다.
  펌웨어에 테스트용 공개 함수나 조건부 코드를 추가하지 않습니다.
- handler는 전달된 프레임과 잠금 상태를 기록합니다. 실제 트랙 보정·timeout 의미는 여기서 검증하지 않습니다.
- 모사 IRQ와 atomic은 실제 동시성·메모리 순서·하드웨어 overrun을 재현하지 않습니다.

따라서 이 테스트 통과는 실제 보드의 IRQ 타이밍·overflow 빈도·손실량·재동기화 성공을 보장하지 않습니다.
보드에서는 정상 송신 → 강제 overflow → 정상 송신 복귀 순서로 복구와 손실을 따로 확인해야 합니다.
