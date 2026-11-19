# 문서 안내

[English overview](../README.md) · [Architecture in English](../EXPLAINER.en.md)

[한국어 프로젝트 소개](../README.ko.md)에서 전체 구성을 확인한 뒤, 목적에 맞는 문서를 선택하세요.

## 처음 보는 분께

1. [구현 흐름](../EXPLAINER.md) — PC 입력부터 ISR·링버퍼·스레드·추정 결과 반환까지
2. [V1 완료 범위](roadmap.md) — 구현한 범위, 검증 근거, 후속 확장
3. [시스템 역할](system-roles.md) — PC·펌웨어의 책임과 측정/트랙 ID 정책

## 실행과 사용

| 목적 | 안내 |
|---|---|
| 보드 연결·펌웨어 빌드·기본 통신 | [보드 시작 안내](board-bringup.md) |
| GUI 연결·미리보기·그래프·상태 조회 | [GUI 사용법](../pc_sim/GUI.md) |
| 측정 송신·CSV·잡음/이상치 평가 | [PC CLI](../pc_sim/README.md) |
| 로그 레벨·디버그 빌드 | [로깅 안내](logging.md) |

## 설계와 테스트

| 목적 | 문서 |
|---|---|
| UART 규격·요구사항·구현·테스트 연결 | [요구사항 추적표](requirements-traceability.md) |
| 자동 테스트 실행과 검증 범위 | [테스트 목록](test-cases.md) |
| RTT·보드 지연·큐·스택의 측정 방법 | [성능 계측](performance-monitoring.md) |
| C/Python 작성 규칙·API 계약 | [코딩 스탠다드](coding-standard.md) |

## 실보드 검증 결과

| 확인할 내용 | 기록 |
|---|---|
| 전체 검증 현황과 날짜별 이력 | [검증 기록](verification-history.md) |
| 정상·잡음·누락 추적 평가, GUI·CSV | [고정 시나리오 평가](hardware-test-2026-11-18-evaluation.md) |
| 잡음100회·이상치49회, 짧은 RTT 회귀 | [확장 추적 평가](hardware-test-2026-11-19-noise-spike.md) |
| 30분 송수신·지연·스택 관측 | [성능 시험](hardware-test-2026-11-19-performance.md) |
| 링 overflow·RX/TX 포화·오래된 측정 복귀 | [전송 오류 시험](hardware-test-2026-11-18-transport-stress.md) |
| INIT/RESET 유실·FIFO 순서·TRACK TX 포화 | [제어·TRACK 시험](hardware-test-2026-11-18-track-controls.md) |
| 데이터 선 단절·재연결 | [물리 통신 장애 시험](hardware-test-2026-11-14-link-loss.md) |

수치는 각 기록의 대상 버전·설정·입력 조건에 한해 해석합니다. host 테스트와 실보드 시험을 구분하며, 과거 실패 판정은 이후 기준 변경으로 덮어쓰지 않습니다. 원시 CSV·콘솔·해시와 재평가 명령은 해당 시험 문서에서 연결합니다.

## 과거 개발 점검

다음 문서는 당시 구조·이름·주석 정리의 판단 근거입니다. 현재 실행 안내나 미완료 작업 목록으로 읽지 않습니다.

- [구조·이름 점검](naming-review.md) · [초기 구조 검토](structure-review-2026-11-14.md)
- [코딩 규칙 적용 점검](coding-standard-audit.md) · [주석 점검](comment-audit.md)

현재 후속 작업 후보는 [로드맵](roadmap.md#다음-구현-순서)에서 관리합니다.
