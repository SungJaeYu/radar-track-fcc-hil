# 펌웨어 코딩 스탠다드 초안

2026-11-08 합의한 목표 규칙입니다. 기존 코드 전체에 적용된 상태는 아니며,
펌웨어 작성자가 이후 리팩터링합니다. 현재 동작·프로토콜 숫자 값은 보존합니다.

## 이름

| 대상 | 규칙 | 예시 |
|---|---|---|
| 함수·일반 변수·구조체 태그 | snake_case | `protocol_frame_encode`, `payload_length`, `struct protocol_frame` |
| 입력·참조 포인터 | `p_` 접두사 | `const struct protocol_frame *p_frame` |
| 결과를 기록하는 출력 포인터 | `out_` 접두사 | `uint8_t *out_buffer`, `size_t *out_length` |
| enum 타입 | PascalCase, 약어도 단어처럼 표기 | `FrameType`, `ParserState`, `RxOverflowPolicy` |
| enum 열거자 | enum 타입명을 UPPER_SNAKE_CASE로 변환한 접두사 + 항목명 | `FRAME_TYPE_INIT`, `PARSER_STATE_WAIT_STX`, `RX_OVERFLOW_POLICY_DRAIN_BUFFER` |
| 매크로·상수 | UPPER_SNAKE_CASE, 모듈 접두사 권장 | `PROTOCOL_FRAME_OVERHEAD`, `CRC16_INITIAL_VALUE` |

출력 포인터에는 `p_`를 중복하지 않고 `out_`만 사용합니다.
포인터가 가리키는 상태 객체를 갱신하는 것과 별도 결과를 출력하는 것은 구분합니다.
예를 들어 `parser_feed(struct frame_parser *p_parser, ...)`는 상태 객체 참조이고,
디코더의 `struct radar_measurement *out_measurement`는 결과 출력입니다.
읽기 전용 입력에는 가능한 경우 `const`를 붙입니다.
배열로 선언한 지역 송신 버퍼는 일반 변수이며 `buffer`처럼 표기합니다.
헤더 선언과 구현의 매개변수 이름도 일치시킵니다.

## 중괄호와 공백

함수의 여는 중괄호는 다음 줄에 둡니다. 제어문의 여는 중괄호는 같은 줄에 둡니다.
`if`, `for`, `while`, `switch`와 여는 괄호 사이에는 공백 한 칸을 둡니다.
들여쓰기는 공백 4칸이며, 줄 끝 공백을 제거하고 파일 끝에 개행을 둡니다.

```c
int example(const struct protocol_frame *p_frame, uint8_t *out_buffer)
{
    if (p_frame == NULL || out_buffer == NULL) {
        return -EINVAL;
    }

    for (size_t i = 0; i < p_frame->payload_length; ++i) {
        out_buffer[i] = p_frame->payload[i];
    }

    return 0;
}
```

위 코드는 표기 예시이며 실제 복사 API에는 출력 용량 검증도 필요합니다.

## 오류 반환

실패를 반환하는 API는 `<errno.h>`의 오류 코드를 음수로 반환합니다.
별도의 `ERROR_*` 상수나 프로젝트 오류 enum은 만들지 않습니다.
임의의 `-1`은 의미가 드러나는 errno로 정리합니다.

| 반환값 | 의미와 적용 예 |
|---|---|
| `0` | 일반 상태 반환 API 성공, 예: init·큐 제출 |
| `-EINVAL` | NULL 인자·잘못된 payload 길이 |
| `-ENOSPC` | 인코더 출력 버퍼 용량 부족 |
| `-ENOMSG` | 현재 `k_msgq_put(..., K_NO_WAIT)`에서 큐 포화 |
| `-ENODEV` | 사용할 장치가 준비되지 않음 |

Zephyr API에서 받은 오류는 의미가 맞으면 원래 음수 errno를 그대로 전달합니다.
호출부는 문서화된 errno별로 처리하고, 큐 포화와 잘못된 인자를 별도 집계합니다.
예상하지 못한 음수 오류는 원래 코드와 함께 진단합니다.
인코더처럼 성공 시 바이트 수를 반환하는 함수는 비음수 성공 값과 음수 오류를 구분하고,
이 반환 계약을 헤더에 명시합니다. errno는 호출의 반환값으로 전달하며 전역
`errno` 변수를 설정하는 방식과 혼동하지 않습니다.

`bool` 판정 함수와 `ParserResult` 같은 처리 결과 enum까지 기계적으로 변환하지 않습니다.
조립 중·완성·CRC 오류 등 상태 머신 결과는 해당 enum으로 표현합니다.

이 문서 추가만으로 기존 반환값 계약을 바꾸지 않습니다.
초기화 함수의 `-1` 등을 정리할 때 선언·구현·호출부·통계·테스트를 함께 확인합니다.

## 헤더·규격·주석

- 헤더는 단독 include가 가능해야 합니다. `size_t`는 `<stddef.h>` 등 필요한 타입 헤더를 직접 포함합니다.
- 구현은 사용하는 함수의 헤더를 직접 포함합니다. 예: `memcpy`는 `<string.h>`.
- STX·프레임 추가 길이 등 공유 규격은 공통 헤더 한 곳에 정의합니다.
- 주석은 영어 또는 영어·한국어 병기를 사용합니다.
- 공개 함수에는 입력 유효성 전제·버퍼 용량·반환값·데이터 복사/소유권을 설명합니다.
- 주석은 실제 IRQ 보호 범위와 스레드 책임에 맞춥니다.

## 적용 TODO

- [ ] 포인터·출력 포인터 이름과 선언/정의 매개변수 이름 통일.
- [ ] enum 타입명·열거자 접두사 통일. 외부 프로토콜 숫자 값 보존.
- [ ] 함수/제어문 중괄호·공백·들여쓰기·파일 끝 개행 정리.
- [ ] init 등의 임의 `-1` 반환을 음수 errno로 통일하고 호출부·통계·회귀 테스트 갱신.
- [ ] 헤더 의존성·공통 상수·오래된 주석 정리.

리팩터링 후 host 테스트와 펌웨어 빌드를 각각 확인합니다.
표기 정리와 새 동작 구현은 검증 범위를 구분합니다.
