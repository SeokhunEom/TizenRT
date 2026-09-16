# Health Monitor 단계별 구현·검토·커밋 계획

[설계 기준](../HealthMonitorImplementationPlan.md)의 기능을 사용자가 순차적으로 확인할 수 있도록 분리한 실행 계획이다. 설계 기준은 동작 정책을, 이 문서는 진행 순서와 확인 지점을 관리한다.

1~3단계는 사용자 검토·커밋을 완료했다. 4단계 tick 검사·PANIC·reboot reason 연결도 구현·검증과 셀프 리뷰 후 사용자 커밋 승인을 받아 본 커밋으로 완료한다. 5단계 이후 구현은 시작하지 않았다.

## 진행 방식

1. 사용자가 시작할 단계를 지정하면 설계 기준, 이 문서, 해당 단계 문서를 읽는다. 앞 단계의 확인·커밋 상태와 현재 작업 트리도 확인한다.
2. 지정한 단계의 변경과 검증을 완료한다. 필요한 빌드 연결과 해당 기능의 검증은 같은 단계에 포함한다.
3. 변경 파일, 구현한 동작, 검증 결과, 미실행 항목, 확인이 필요한 설계 선택을 사용자에게 제시한다. 검토할 수 있는 diff가 준비된 상태에서 확인을 요청한다.
4. 사용자가 확인하면 해당 단계의 관련 파일만 커밋한다. 검토 의견에 따른 수정은 해당 단계에서 마무리한다.
5. 커밋 해시와 다음 단계를 안내하고 대기한다. 사용자가 커밋과 다음 단계 진행을 함께 지시했다면 그 지시대로 이어간다.

단계별 사용자 확인은 사용자가 요청한 작업 방식이다. 구현 중 다음 단계 코드를 미리 추가하거나 여러 단계를 합쳐 한 번에 커밋하지 않는다. 범위 변경이 필요하면 근거와 변경안을 현재 단계의 검토 내용에 명시한다.

기본 단위는 단계당 커밋 하나다. 앞 단계의 커밋은 검토 지점으로 보존한다. 이전 커밋 수정이나 단계 재분할이 필요하면 사용자와 범위를 맞춘다. 새 테스트는 해당 기능을 구현하는 단계에서 필요한 만큼 추가하고, 마지막 단계에서는 통합 시나리오와 측정 결과를 보완한다.

## 중간 단계의 동작

- 각 커밋은 그 단계까지 존재하는 구성으로 빌드 가능하게 유지한다. 아직 구현하지 않은 기능을 성공처럼 보이게 하는 임시 stub은 만들지 않는다.
- 기본 `CONFIG_HEALTH_MONITOR`는 비활성으로 두고, 기준 defconfig의 제품 동작 활성화는 7단계에서 다룬다. 중간 단계 검증에 사용한 활성 설정과 PM 조건은 결과에 명시한다.
- 1~3단계는 인터페이스와 등록 상태를 만드는 단계다. 실제 tick timeout 리셋은 4단계부터, 정상 절전과 HW WDT를 포함한 전체 동작은 5~7단계에서 확인한다.
- 보드·빌드 환경이 없어 수행하지 못한 검증은 미실행으로 기록한다. 빌드 통과, 코드 검토, 호스트 검증, 실제 보드 검증을 구분한다.

## 단계 목록

| 단계 | 문서 | 사용자가 확인할 핵심 | 커밋 메시지 예시 |
|---|---|---|---|
| 1 | [인터페이스·시간 표현·저장공간](01-interface-and-storage.md) | ioctl 형태, timeout 범위, TCB 필드, 메모리 크기 | `health_monitor: define interfaces and task state` |
| 2 | [등록·갱신·해제와 TCB 수명](02-registry-and-lifecycle.md) | kick O(1), 힙 구조, 잠금 범위, 종료 시 제거 | `health_monitor: implement registry and task cleanup` |
| 3 | [앱용 ioctl 드라이버](03-driver-ioctl.md) | 앱 진입, 호출 스레드 식별, fd 공유·close 의미 | `health_monitor: expose task operations through ioctl` |
| 4 | [tick 검사·PANIC·reboot reason](04-timer-and-panic.md) | 최신 deadline 판정, 도래한 후보 전부 처리, 잠금 밖 PANIC | `health_monitor: check deadlines from the system tick` |
| 5 | [PM wakeup 연동](05-pm-wakeup.md) | sleep 시간 포함, 다음 wakeup 선택, 복귀 시 판정 | `health_monitor: include deadlines in PM wakeup` |
| 6 | [HW watchdog 연결](06-hardware-watchdog.md) | 실제 WDT 시작, keepalive 조건, 정상 sleep과 장애 리셋 | `health_monitor: connect tick progress to hardware watchdog` |
| 7 | [통합 검증·기준 설정 활성화](07-integration-validation.md) | SMP·절전·리셋 실측 결과와 최종 적용 설정 | `health_monitor: validate and enable the rtl8730e configuration` |

기존 계획 문서는 1단계 구현 커밋에 함께 포함한다. 후속 단계의 계획 문서가 포함돼 있어도 해당 단계의 구현 완료를 의미하지 않는다.

## 단계별 제출 내용

각 단계 종료 시 다음 내용을 한 번에 제시한다.

- 구현한 범위와 변경 파일
- 해당 단계 문서의 사용자 확인 항목에 대한 답과 실제 코드 위치
- 실행한 검증의 명령·설정·결과, 미실행 검증과 이유
- 다음 단계까지 남아 있는 동작과 이번 단계에서 발견한 설계 변경 필요 사항
- 커밋할 파일 범위와 제안 커밋 메시지

사용자 확인 전에는 다음 단계 구현을 시작하지 않는다. 확인 후 커밋하면 아래 진행표에 커밋과 확인 결과를 기록한다. 기록은 다음 문서 갱신 때 반영하며, 해시 기록만을 위해 검토받은 커밋을 amend하지 않는다.

## 진행표

| 단계 | 상태 | 검증·사용자 확인 | 커밋 |
|---|---|---|---|
| 1 | 완료 | [구현·검증 결과](01-interface-and-storage.md). TCB 내장 + 내부 접근 함수로 사용자 확인 완료. 커널 OFF/ON 빌드 통과, 전체 Kconfig 검증 제한 있음 | `9d4f19a19` |
| 2 | 완료 | [구현·검증 결과](02-registry-and-lifecycle.md). OFF/ON 커널 빌드, 호스트 테스트·ARM 컴파일 통과. 사용자도 보드 빌드·실행에 문제가 없다고 보고했으나 등록·SMP 수명 시나리오는 미검증 | `d76d143c8` |
| 3 | 완료 | [구현·검증 결과](03-driver-ioctl.md). 사용자 커밋 승인. 실제 driver/VFS를 포함한 UP/SMP 호스트 테스트, ON/OFF kernel·driver 빌드와 보드 초기화 객체, 공개 헤더 기반 ARM C/C++ 예제 컴파일 통과. 보드 ioctl 실행은 미검증 | `7f1e3a003` |
| 4 | 완료 | [구현·검증 결과](04-timer-and-panic.md). Linux ASan/UBSan 7종, ARM kernel ON/OFF·엄격 컴파일·reason OFF 구성 및 추가 모델 비교 통과. 셀프 리뷰 후 사용자 커밋 승인. 실제 보드 PANIC·리셋·실행 시간은 미검증 | 본 커밋: `health_monitor: check deadlines from the system tick` |
| 5 | 대기 | — | — |
| 6 | 대기 | — | — |
| 7 | 대기 | — | — |

기능 검증의 전체 목록은 [설계 기준의 검증 계획](../HealthMonitorImplementationPlan.md)을 사용한다. 각 단계 문서에는 그 단계에서 확인할 항목만 둔다.
