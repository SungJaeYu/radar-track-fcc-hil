# VS Code 설정 예제

macOS + Xcode/Command Line Tools + Homebrew CMake/Ninja + `~/zephyrproject` 환경용입니다.
실제 `.vscode/`는 개인 설정으로 Git에서 제외합니다. 저장소 루트에서 초기 설정을 복사합니다.
기존 설정이 있다면 필요한 항목을 병합하세요.

```sh
mkdir -p .vscode
cp tools/vscode/*.json .vscode/
```

1. `Zephyr: Build firmware`로 `build/compile_commands.json`과 생성 헤더를 갱신합니다.
2. `Host: Configure UART tests`, `STATUS tests`, `TX tests`, `Tracking tests`를 실행합니다.
   각 작업은 host CMake 구성과 compile database 생성만 수행하며 테스트 실행은 별도입니다.
3. C/C++ 확장은 firmware database를 먼저, 각 host database를 이후 순서로 참조합니다.
   ARM/Zephyr 설정을 host mock 테스트에 전역 강제하지 않습니다.
4. 파일 이동 후 오류가 남으면 `C/C++: Reset IntelliSense Database`를 실행하고
   `C/C++: Log Diagnostics`에서 해당 소스의 compile database 연결을 확인합니다.

`fcc_app/src` 하나를 include root로 사용합니다. 예: `"transport/uart_rx.h"`.
헤더 분석은 연결된 translation unit 설정을 따릅니다. compile database가 생성되기 전에는
fallback includePath만으로 Zephyr 생성 헤더·매크로를 모두 해석할 수 없습니다.

CMake/Ninja가 PATH에 없거나 Xcode 설치 위치·Zephyr workspace가 다르면 tasks.json의 경로를 조정합니다.
추가 소스는 CMake에 등록하고 다시 빌드하세요. compile database는 생성물이며 커밋하지 않습니다.

## Kconfig 변경 후 CONFIG 매크로 오류

`CONFIG_...`는 Zephyr 구성 단계가 생성하는
`build/zephyr/include/generated/zephyr/autoconf.h`에 정의됩니다.
compile_commands.json의 `-imacros` 옵션으로 적용되므로 Kconfig/prj.conf 변경 후에는 다시 빌드해야 합니다.

`Tasks: Run Task` → `Project: Refresh IntelliSense`를 실행하면 펌웨어 빌드와 네 host 테스트 구성을 순서대로 갱신합니다.
오류가 남으면 `C/C++: Reset IntelliSense Database`를 실행하고 파일을 다시 여세요.
`C/C++: Log Diagnostics`에서 해당 소스의 compile database 연결도 확인하세요.

host 테스트는 Kconfig를 실행하지 않으므로 해당 CMake target에서 CONFIG 값을 전달합니다.
전역 `defines`에 값을 복사하거나 생성된 autoconf.h를 직접 수정하지 마세요.
실제 빌드와 에디터가 서로 다른 값을 사용하게 될 수 있습니다.
