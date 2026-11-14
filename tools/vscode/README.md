# VS Code 개발 환경 안내

VS Code는 선택 사항입니다. 빌드·테스트는 저장소의 CLI 명령으로 실행할 수 있습니다.
개인 에디터 설정인 `.vscode/`와 생성된 빌드 파일은 Git에서 제외합니다.
이 디렉터리에는 설정 JSON을 배포하지 않고, 환경을 연결하는 방법만 유지합니다.

## 필요한 확장

- C/C++ 분석: Microsoft C/C++ (`ms-vscode.cpptools`).
- Kconfig 문법 강조: `luveti.kconfig` (선택).

```sh
code --install-extension ms-vscode.cpptools
code --install-extension luveti.kconfig
```

`code` 명령이 PATH에 없으면 VS Code 확장 화면에서 같은 ID로 검색해 설치합니다.

## 빌드 정보로 IntelliSense 구성

컴파일러 경로·include 경로·CONFIG 매크로를 수작업으로 복제하지 않고
CMake가 생성하는 `compile_commands.json`을 사용합니다.

1. [펌웨어 빌드](../../README.md#펌웨어-빌드플래시)에
   `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON`을 전달합니다.
2. host 테스트도 각 CMake 구성 명령에 같은 옵션을 전달합니다.
   [RX](../../fcc_app/tests/test_uart_rx/README.md),
   [handler](../../fcc_app/tests/test_frame_handler/README.md),
   [TX](../../fcc_app/tests/test_uart_tx/README.md),
   [Tracking](../../fcc_app/tests/test_kalman/README.md)의 명령을 따릅니다.
3. 로컬 C/C++ 설정의 `compileCommands`에 생성된 데이터베이스 경로를 연결합니다.
   기본 펌웨어 분석은 일반 빌드, 디버그 전용 분석은 debug.conf 빌드의 데이터베이스를 선택합니다.

권장 빌드 디렉터리 이름을 사용할 경우 연결 대상은 다음과 같습니다.

| 대상 | 컴파일 데이터베이스 |
|---|---|
| 일반 펌웨어 | `build/compile_commands.json` |
| 디버그 펌웨어 | `build_logging_debug/compile_commands.json` |
| RX host | `build_uart_rx/compile_commands.json` |
| handler host | `build_frame_handler/compile_commands.json` |
| TX host | `build_uart_tx/compile_commands.json` |
| Tracking host | `build_tracking/compile_commands.json` |

다른 빌드 디렉터리를 사용하면 경로도 맞춰 변경합니다. 일반·디버그 설정을 동시에
하나의 기준으로 해석하지 않고, 현재 분석할 펌웨어 구성을 명확히 선택합니다.
디버그 빌드 방법과 로그 레벨은 [로깅 안내](../../docs/logging.md)를 참고합니다.

## 펌웨어와 host 환경 구분

펌웨어는 ARM 도구체인과 실제 Zephyr 헤더를 사용하고, host 테스트는 PC 컴파일러와
테스트용 모사 헤더를 사용합니다. ARM 경로·매크로를 모든 파일에 전역 적용하지 않습니다.
예를 들어 `zephyr/logging/log.h`는 host에서
`fcc_app/tests/common/include/zephyr/logging/log.h`로 해석해야 합니다.

일부 host 테스트는 제품 C 파일을 직접 포함하므로 같은 파일도 분석 문맥이 달라질 수 있습니다.
오류가 생기면 파일을 연 상태에서 `C/C++: Log Diagnostics`로 실제 선택된
컴파일러·데이터베이스·include 경로를 확인합니다. 설정 변경 뒤에는
`C/C++: Reset IntelliSense Database`로 재분석할 수 있습니다.
실제 컴파일 실패와 에디터 진단은 구분합니다.

## Kconfig와 생성 헤더

Kconfig는 C/C++ 파일이 아닙니다. `luveti.kconfig` 설치 후 `Kconfig`, `Kconfig.*`의
언어 모드를 `kconfig`로 지정합니다. 이전 `plaintext` 또는 C++ 연결이 남아 있으면
로컬 파일 연결을 수정하고 파일을 다시 엽니다. 문법 강조·접기는 지원하지만
자동 포맷터나 Zephyr 의미 검증을 제공하는 확장은 아닙니다. 현재 파일은 공백 4칸을 사용합니다.

`CONFIG_*`는 Zephyr 구성 단계에서 `zephyr/include/generated/zephyr/autoconf.h`에
생성되고 컴파일 데이터베이스의 `-imacros`로 적용됩니다. Kconfig/prj.conf 변경 후에는
다시 빌드합니다. 생성 헤더를 직접 수정하거나 동일한 값을 에디터의 `defines`에 복사하지 않습니다.
host 테스트는 Kconfig를 실행하지 않고 각 CMake target에서 필요한 설정값을 전달합니다.
