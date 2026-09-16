# Health Monitor 구현 계획 초안

- 작성일: 2026-09-15
- 1차 적용 대상: `rtl8730e/loadable_ext_ddr_st7785`
- 상태: 동작 정책과 기본 구현 방향 합의 완료. 구현 진행 상태는 [단계별 진행표](health-monitor/README.md#진행표)에서 관리한다.
- 이 문서는 설계 기준이며, 단계별 구현 결과와 검증 기록은 해당 단계 문서에 남긴다.

이 문서는 기능과 동작 정책의 기준 문서다. **구현을 시작하거나 재개할 때는 [단계별 구현·검토·커밋 계획](health-monitor/README.md)을 먼저 읽고, 사용자가 지정한 한 단계만 구현한다.** 각 단계의 변경사항과 검증 결과를 사용자에게 제시한 뒤 확인받아 커밋한다.

## 1. 목적과 범위

주요 스레드가 자신을 등록하고 주기적으로 kick하는 커널 수준의 SW watchdog을 구현한다. 판정 조건은 **등록된 스레드의 최신 deadline이 검사 시점에 지났는가** 하나다. 세마포어 deadlock 등 kick이 중단된 원인은 구분하지 않는다.

우선순위는 다음과 같다.

1. 감시 기능이 새로운 교착이나 무한 대기를 유발하지 않도록 한다.
2. kick과 평상시 tick 처리는 O(1)로 유지한다.
3. 장애 시 기존 PANIC 진단과 리셋 경로를 활용한다.

감시 기능의 저장공간은 정적 메모리와 TCB 내 필드를 사용한다. 새로운 malloc, 세마포어 대기, 뮤텍스 대기, sleep, worker 실행 의존성을 추가하지 않는다. 짧은 로컬 IRQ 마스킹과 전용 spinlock은 허용한다. 기존 open 경로의 blocking 처리는 start·stop·kick 경로의 제약에서 제외한다.

다음 기능은 초안에 포함하지 않는다.

- 등록되지 않은 전체 스레드에 대한 감시
- sched_lock 장기 점유의 독립적인 판정
- 높은 우선순위 CPU 독점, 동일 PID 연속 실행, 굶는 태스크 탐색
- WAIT_SEM 상태를 이용한 deadlock 원인 판정이나 semaphore holder 추적
- 추가 콜스택 수집, 별도 진단 로그 수집 체계, 주기적인 경고 로그
- 별도의 100ms 탐지 지연 제한 및 그 제한 초과에 따른 SW 장애 판정
- ISR에서 후보를 4개 또는 8개씩 처리하는 고정 개수 제한

sched_lock이나 CPU 독점 때문에 등록된 스레드가 kick하지 못하면 일반 deadline 검사로 탐지할 수 있다. 이는 해당 원인을 독립적으로 탐지하거나 모든 CPU의 실행 정지를 보장한다는 의미는 아니다.

## 2. 기준 시스템

기준 설정은 [loadable_ext_ddr_st7785/defconfig](../build/configs/rtl8730e/loadable_ext_ddr_st7785/defconfig)다.

| 항목 | 현재 설정 또는 코드 동작 | 설계 반영 |
|---|---|---|
| 빌드 | Protected build, 앱 바이너리 분리 | 앱은 전용 드라이버 ioctl로 진입 |
| SMP | 2코어 | 두 CPU에서 등록·갱신·해제 가능 |
| 시스템 tick | 1ms, 시스템 시간은 64비트 | 기존 시스템 시간을 사용 |
| timer ISR | CPU0에서 시스템 tick 실행 | CPU0에서 만료 후보 검사 |
| 최대 태스크 | `CONFIG_MAX_TASKS=256` | 힙을 기존 태스크 상한에 맞춰 정적 확보 |
| PM | tick suppression, timed wakeup 활성화 | sleep 경과 시간 포함 및 wakeup 연동 |
| PANIC | `CONFIG_BOARD_ASSERT_AUTORESET=y` | 기존 진단·자동 리셋 경로 활용 |
| 리붓 사유 | `CONFIG_SYSTEM_REBOOT_REASON=y` | health monitor timeout 사유 추가 |
| HW watchdog | 드라이버 활성화, `WATCHDOG_FOR_IRQ` 비활성화 | 필요 시 활성화하고 실제 시작·갱신 경로 연결 |

주요 연결 지점은 [sched_process_timer()](../os/kernel/sched/sched_processtimer.c), [보드 timer ISR](../os/arch/arm/src/amebasmart/amebasmart_timerisr.c), [PM idle 처리](../os/pm/pm_idle.c)다. 현재 timer ISR은 누락된 tick을 한 ISR에서 반복 처리할 수 있으므로 성능 검증에 이 동작도 포함한다.

## 3. 호출 인터페이스와 수명

### 3.1 드라이버 진입

전용 character driver를 등록한다. 장치 경로는 `/dev/health_monitor`를 제안하며, ioctl 번호와 최종 명칭은 구현 시 기존 규칙에 맞춰 정한다.

| 명령 | 인자 | 동작 |
|---|---|---|
| START | `timeout_ms` | 호출 스레드 자신을 등록 |
| KICK | 없음 | 호출 스레드 자신의 deadline 갱신 |
| STOP | 없음 | 호출 스레드 자신의 감시 해제 |

앱은 초기화 시 open하고 fd를 보관해 ioctl을 호출한다. 새 syscall을 추가하지 않으며 기존 ioctl 진입 경로를 사용한다. 커널 스레드는 같은 내부 감시 함수를 직접 호출할 수 있게 한다.

감시의 소유자는 fd가 아니라 호출 스레드다. open 자체는 등록하지 않으며, 같은 fd를 여러 스레드가 사용하더라도 각 ioctl은 호출 스레드 자신에게 적용한다. close에 전체 감시 해제를 연결하지 않고 STOP 또는 스레드 종료에서 정리한다. 감시 함수는 스레드 문맥에서 호출한다.

현재 [ioctl 공통 경로](../os/fs/vfs/fs_ioctl.c)는 드라이버의 ioctl 함수로 전달한다. 기존 [HW watchdog 드라이버](../os/drivers/watchdog.c)의 ioctl에는 `sem_wait()`가 있으므로, health monitor 명령은 전용 드라이버에서 내부 감시 함수로 직접 전달한다.

### 3.2 기본 반환 정책

- START 성공: 0. 이미 등록된 경우 `-EEXIST`, 지원하지 않는 timeout은 `-EINVAL`.
- 정적 힙 용량에 도달한 START: `-ENOSPC`. `CONFIG_MAX_TASKS` 크기의 배열 경계를 보호하며, 더 작은 별도 등록 제한을 추가하지 않는다.
- KICK: 등록되지 않은 경우 아무 동작 없이 성공. 등록된 경우 deadline 갱신.
- STOP 성공: 0. 등록되지 않은 경우 `-ENOENT`.
- 지원하지 않는 ioctl 명령: `-ENOTTY`.
- 위 음수 오류는 커널 내부/드라이버 기준이다. 앱에는 기존 ioctl의 반환값과 errno 규칙을 따른다.

최소·최대 timeout 값은 tick 변환과 시간 비교가 안전한 범위로 구현 시 정한다. 최초 제안의 내부 함수 형태인 `health_monitor_start(timeout_ms)`, `health_monitor_kick()`, `health_monitor_stop()`을 유지할 수 있다. 앱 wrapper의 형태는 드라이버 인터페이스에 맞춰 정한다.

### 3.3 kick, stop, 종료와 만료의 순서

KICK에서는 기존 deadline의 만료 여부를 검사하지 않는다. ISR의 검사 전에 kick이 처리되면 이미 시간이 지났더라도 새 deadline을 인정한다.

| 처리 순서 | 결과 |
|---|---|
| 기존 deadline 경과 → KICK → ISR 검사 | 갱신된 deadline으로 판정 |
| 기존 deadline 경과 → ISR에서 만료 확정 → KICK | 이미 확정한 장애는 취소하지 않음 |
| STOP 또는 종료 처리 → ISR 검사 | 감시 대상에서 제거됐으므로 검사하지 않음 |

스레드가 정상 종료하거나 다른 스레드에 의해 종료되면 TCB 해제 전에 감시를 제거한다. 종료 경로는 호출자용 STOP 대신 대상 TCB를 받는 내부 정리 함수를 사용한다. 중복 정리는 안전하게 처리한다.

## 4. 저장공간과 최소 힙

### 4.1 자료구조

| 위치 | 필드 | 역할 |
|---|---|---|
| TCB 내부 | `deadline`, `timeout` | 실제 만료 시각과 설정 timeout. `timeout == 0`은 미감시 |
| 정적 최소 힙의 항목 | TCB 포인터, `check_at` | 대상과 다음 검사 예약 시각 |
| 전역 상태 | 힙 개수, 최소 검사 시각의 사본, 전용 spinlock 등 | 빠른 진입 판단과 동기화 |

힙 크기는 `CONFIG_MAX_TASKS`로 한다. 별도의 더 작은 등록 제한이나 동적 확장은 두지 않는다. STOP은 O(N) 탐색을 허용하므로 초안에서는 TCB에 힙 인덱스 필드를 추가하지 않는다.

32비트 필드와 포인터 기준의 예상 추가 메모리는 다음과 같다.

- TCB 필드 8바이트 × 256개: 약 2KB
- 힙 항목 8바이트 × 256개: 약 2KB
- 전역 상태: 소량의 고정 메모리

실제 크기와 정렬은 빌드 시 확인한다. TCB 필드는 `CONFIG_HEALTH_MONITOR`로 조건부 포함한다.

초안은 TCB 직접 저장을 유지한다. 저장 위치를 나중에 바꾸기 쉽게 감시 모듈의 상태 접근은 내부 `health_monitor_state(tcb)`로 통일하고, `tcb->health_monitor` 직접 접근은 이 함수에만 둔다. 함수는 유효한 PID가 배정된 살아 있는 TCB의 상태 주소만 계산하며, 상태 읽기·쓰기는 전용 잠금 아래에서 수행한다. 힙은 위의 TCB 포인터와 `check_at`을 유지하고 감시 상태 포인터를 별도로 장기 보관하지 않는다.

향후 PID 슬롯별 정적 배열로 전환할 경우 저장공간 정의와 접근 함수, 초기화·정리를 조정한다. 공개 ioctl과 힙·만료 판정 정책은 유지한다. 현재 커널의 PID 슬롯 유일성에 의존하므로 전환 시 PID 재사용·재시작·SMP 종료 경합을 다시 검증한다. 초안에 두 저장 방식을 선택하는 설정이나 함수 포인터 계층을 추가하지 않는다.

### 4.2 실제 deadline과 검사 예약 시각 분리

START 시 `deadline`과 `check_at`을 동일하게 설정한다. KICK은 실제 `deadline`만 연장하고 힙 순서는 바꾸지 않는다. 따라서 힙의 최소값은 정확한 최신 만료 시각이 아니라 가장 이른 **검사 예약 시각**이다.

검사할 때 실제 deadline이 미래이면 `check_at`을 최신 deadline으로 옮기고 힙을 재정렬한다. 예약 시각이 실제 만료 시각보다 이르게 남는 것은 허용하며, 이로 인해 필요한 시각보다 늦게 검사하도록 예약되지는 않는다.

빈 힙은 활성 여부나 개수로 구분한다. tick 값 0을 특별한 무효 deadline으로 간주하지 않는다.

### 4.3 시간 표현

health monitor 전용 `tick++` 카운터를 만들지 않고 sleep 복귀 시 보정되는 기존 시스템 시간을 사용한다.

기준 설정은 `CONFIG_SYSTEM_TIME64=y`이므로 32비트 CPU에서 64비트 시간을 단순히 한 번의 원자적 읽기라고 가정하지 않는다. [clock_systimer()](../os/kernel/clock/clock_systimer.c)와 사용 가능한 시간 접근 경로를 확인한다. TCB와 힙에 32비트 tick을 저장하는 경우 wraparound를 고려한 비교와 지원 timeout 범위를 함께 정한다. 시간 비교 방식은 deadline 판정, 힙 정렬, PM 지연 계산에서 일치시킨다.

## 5. 실행 흐름과 비용

N은 현재 등록된 감시 대상 수다. 아래 비용은 자료구조 처리의 알고리즘 비용이며 spinlock 경합으로 인한 대기시간은 별도다.

| 경로 | 처리 | 비용 |
|---|---|---|
| START | timeout 검증, TCB 설정, 힙 삽입 | O(log N) |
| KICK | 등록 여부 확인, 실제 deadline 갱신 | O(1) |
| STOP | 힙에서 대상 탐색, 제거 및 재정렬 | O(N) |
| 평상시 tick | 최소 검사 시각 비교 | O(1) |
| 검사 시각 도래 | 후보 K개 확인·재정렬 | O(K log N) |

START·STOP은 호출 스레드가 직접 정렬 자료구조를 수정한다. KICK 때문에 늦춰진 검사 예약 시각은 CPU0가 갱신한다. 별도 worker나 CPU0 요청 큐는 만들지 않는다.

CPU0의 timer 처리 흐름은 다음과 같다.

1. 최소 검사 예약 시각이 아직 미래이거나 힙이 비어 있으면 종료한다.
2. 전용 잠금 획득을 시도한다. 실패하면 ISR에서 기다리지 않고 다음 tick에 재시도한다.
3. 잠금 아래에서 힙을 다시 확인하고 이번 검사의 기준 시각 `now`를 고정한다.
4. 루트의 `check_at`이 `now`보다 미래이거나 힙이 비었으면 검사를 끝낸다.
5. 루트 대상의 최신 deadline이 `now` 이하이면 만료를 확정한다. 잠금을 풀고 장애 처리로 진입한다.
6. 최신 deadline이 미래이면 루트의 `check_at`을 해당 deadline으로 바꾸고 재정렬한 뒤 4번으로 돌아간다.

검사 조건은 예약 시각의 동일 여부가 아니라 `check_at <= now`다. 재정렬한 정상 대상은 고정한 `now`보다 미래로 이동하므로 이번 검사에서 같은 대상을 반복 처리하지 않는다.

도래한 후보는 한 번에 모두 처리한다. 최악에는 등록된 모든 대상을 처리할 수 있으며 이 경우 O(N log N)이다. 매 tick 전체 TCB나 전체 감시 목록을 탐색하는 방식은 사용하지 않는다. 탐지 지연에 별도 수치 한도를 두지 않고 가능한 첫 검사에서 처리한다.

## 6. SMP와 TCB 수명 보호

- 전용 spinlock 하나로 START·KICK·STOP, ISR의 대상 확인, 종료 시 제거를 보호한다.
- 스레드 경로는 로컬 IRQ를 막은 상태에서 잠금을 획득하고, 작업 후 잠금을 풀고 IRQ 상태를 복구한다.
- ISR은 잠금 획득에 실패하면 반환한다. 사용하는 trylock 구현에 숨은 재시도나 계측 경로가 있는지도 확인한다.
- 잠금 내부에서는 힙과 감시 필드에 대한 제한된 메모리 처리만 수행한다. 스케줄러 공용 잠금 획득, 메모리 할당, 로그 출력, PANIC을 호출하지 않는다.
- 정상 tick의 빠른 비교는 안전하게 게시한 최소 검사 시각의 사본을 사용한다. 잠금 없이 힙의 TCB 포인터를 역참조하지 않는다.
- 잠금 밖에서 읽는 전역 상태에는 필요한 원자적 접근과 메모리 순서를 적용한다. `volatile`만으로 SMP 일관성이 보장된다고 가정하지 않는다.
- 최소 예약 조회는 `health_monitor_next_check()`의 일관된 사본을 사용한다. 게시 중이면 `-EAGAIN`을 반환하며, 호출자는 이를 빈 목록으로 취급하거나 그 자리에서 반복 대기하지 않는다. timer 검사는 다음 tick으로, PM sleep 진입은 다음 기회로 미룬다.
- PID 반납과 TCB 해제 전에 같은 잠금 아래에서 힙 연결과 감시 상태를 제거한다. task 재시작도 이전 등록을 해제한다. 만료 확정 후 잠금을 풀었다면 해당 TCB 포인터를 추가로 역참조하지 않는다.

종료 경로가 이미 스케줄러 공용 잠금을 보유할 수 있으므로 잠금 순서를 확인한다. health monitor 잠금을 보유한 채 반대 방향으로 공용 커널 잠금을 얻는 경로를 만들지 않는다.

전용 잠금의 짧은 보유 구간과 잠금 순서 검토로 감시 기능 자체의 교착을 방지한다. 일반 spinlock 사용만으로 모든 장애 상태에서의 무한 대기 방지가 증명되는 것은 아니므로, HW WDT 연결과 경합 검증도 함께 수행한다. 별도의 100ms 잠금 대기 감시 기능은 추가하지 않는다.

## 7. PM 연동

정상 sleep 시간도 timeout에 포함한다. 현재 [PM 복귀 경로](../os/pm/pm_idle.c)는 `clock_timer_nohz()`로 누락 시간을 시스템 tick에 반영하므로 health monitor deadline을 sleep 시간만큼 연장하지 않는다.

PM의 다음 wakeup 계산에 health monitor의 최소 검사 예약 시각을 반영한다.

- 기존 wakeup 예약과 health monitor 예약이 모두 있으면 더 이른 시각을 선택한다.
- 어느 한쪽에만 예약이 있으면 그 예약을 사용한다. 기존 코드의 0 또는 무예약 표현을 시간 0과 혼동하지 않는다.
- 이미 검사할 시각이 됐다면 sleep 진입을 보류하고 timer 경로에서 검사하게 한다.
- 예약 시각이 너무 가까운 경우 기존 PM sleep 진입 기준도 적용한다.
- KICK 이전의 예약 시각 때문에 조금 일찍 깨는 것은 허용한다. PM 경로에서 전체 deadline 재탐색이나 힙 갱신을 수행하지 않는다.
- PM이 공용 커널 잠금을 보유하는 점을 고려해 최소 예약 조회에서도 불필요한 잠금 대기를 만들지 않는다.

복귀 직후에도 판정은 동일하다. ISR 검사 전에 KICK이 처리되면 갱신된 deadline을 인정한다. sleep 중 지난 deadline을 별도 위반 이력으로 보존하지 않는다.

## 8. PANIC, reboot reason, HW watchdog

### 8.1 SW timeout 처리

만료 확정 후 전용 잠금을 해제하고 health monitor timeout용 신규 reboot reason을 기록한 다음 기존 PANIC 경로로 진입한다. reason 명칭은 `REBOOT_SYSTEM_HEALTH_MONITOR_TIMEOUT`을 제안하며 숫자는 기존 [reboot reason 목록](../os/include/tinyara/reboot_reason.h)과 충돌하지 않게 구현 시 정한다.

기존 [assert reason 기록 함수](../os/arch/arm/src/common/up_reboot_reason.c)는 이미 기록된 reason이 있으면 assert reason을 덮어쓰지 않는 경로를 갖고 있다. 실제 보드에서 신규 reason이 유지되는지 검증한다.

초안은 기존 PANIC 출력만 활용한다. [ARMv7-A assert](../os/arch/arm/src/armv7-a/arm_assert.c)의 기본 진단 대상은 해당 CPU에서 실행 중이던 스레드이므로, 다른 스레드의 deadline 만료가 그 스레드의 콜스택 출력으로 자동 연결된다고 가정하지 않는다. 이를 위한 추가 덤프 체계는 구현하지 않는다.

### 8.2 HW watchdog 연결

필요하면 `CONFIG_WATCHDOG_FOR_IRQ`를 활성화한다. tick ISR이 실행되지 않아 keepalive가 중단되는 경우 HW WDT가 리셋하는 경로를 사용한다. 기존 HW watchdog reason은 54이며, SW timeout의 신규 reason과 구분한다.

구현 시 다음 연결 사항을 확인한다.

- [보드 watchdog 코드](../os/arch/arm/src/amebasmart/amebasmart_watchdog_lowerhalf.c)의 초기화, 실제 시작, timeout, keepalive 경로
- [보드 부팅 코드](../os/board/rtl8730e/src/rtl8730e_boot.c)의 watchdog 초기화와 IRQ watchdog 설정 사이의 중복 또는 덮어쓰기
- 현재 `sched_process_timer()` 선두의 keepalive와 health monitor 검사 순서
- 만료 확정 후 keepalive 중단 및 기존 PANIC이 진행되지 못할 때의 HW 리셋
- 검사가 잠금 경합으로 연기되는 동안 keepalive를 어떻게 연결할지에 대한 진행 조건
- 정상 sleep 중 HW WDT의 카운트·갱신 동작과 PM wakeup 간의 관계

이 항목은 별도의 SW 지연 임계치를 추가하는 계획이 아니라 기존 HW WDT 연결을 완성하기 위한 확인 사항이다. 설정 활성화나 함수 이름만으로 HW WDT가 실제 시작됐다고 판단하지 않는다. 정상 절전으로 인한 keepalive 공백이 잘못된 리셋을 일으키지 않도록 보드 동작에 맞춰 연동한다.

시스템 tick은 CPU0에서 실행되므로 CPU1만의 IRQ 마스킹을 곧바로 tick keepalive 중단으로 간주하지 않는다. 등록 스레드 deadline 감시와 HW WDT 각각의 실제 동작 범위를 보드에서 확인한다.

## 9. 예상 변경 위치

신규 파일 이름과 디렉터리 구성은 구현 시 저장소 관례에 맞춰 확정한다.

| 위치 | 예정 작업 |
|---|---|
| `os/kernel/health_monitor/` 또는 동등한 신규 커널 파일 | 정적 힙, START·KICK·STOP, timer 검사, 대상 TCB 정리, PM 예약 조회 |
| `os/drivers/health_monitor.c` 및 공개 헤더(신규) | 장치 등록과 ioctl 명령 전달 |
| [TCB 정의](../os/include/tinyara/sched.h) | 조건부 deadline·timeout 필드 |
| [kernel Makefile](../os/kernel/Makefile), [kernel Kconfig](../os/kernel/Kconfig), [driver Makefile](../os/drivers/Makefile), [driver Kconfig](../os/drivers/Kconfig) | 설정과 빌드 연결 |
| [sched_processtimer.c](../os/kernel/sched/sched_processtimer.c) | 공용 잠금 처리 전 감시 진입과 keepalive 순서 연결 |
| [task_exithook.c](../os/kernel/task/task_exithook.c), [sched_releasetcb.c](../os/kernel/sched/sched_releasetcb.c) 및 관련 종료 경로 | TCB 해제 전 감시 제거와 초기화 확인 |
| [pm_idle.c](../os/pm/pm_idle.c) | 다음 wakeup 시각에 health monitor 예약 반영 |
| [reboot_reason.h](../os/include/tinyara/reboot_reason.h) | 신규 timeout reason |
| 보드 초기화, watchdog, 기준 defconfig | 장치 등록과 HW WDT 연동 |

## 10. 구현 순서

실행 순서, 단계별 변경 범위, 사용자 확인 기준, 커밋 단위는 [단계별 계획](health-monitor/README.md)에서 관리한다. 이 문서의 동작 정책을 기준으로 각 단계를 진행하며, 검토 결과 정책을 바꾸면 해당 내용을 이 문서에 반영한다.

전체 기능을 구현한 뒤 한꺼번에 검토하는 방식으로 진행하지 않는다. 현재 단계의 구현·검증을 완료해 사용자에게 제시하고, 확인받은 범위만 커밋한 뒤 다음 단계로 넘어간다.

## 11. 검증 계획

| 항목 | 확인할 결과 |
|---|---|
| 설정 비활성 빌드 | health monitor 조건부 코드와 TCB 필드가 비활성 구성에서도 빌드를 깨뜨리지 않음 |
| 정상 등록·주기적 KICK | 오탐이나 리셋 없이 deadline 갱신 |
| KICK 중단 | 검사 시 deadline 만료를 확인하고 PANIC·리셋 |
| 등록 스레드의 세마포어 대기 | 원인 구분 없이 KICK 중단에 따른 timeout 탐지 |
| 검사 전 늦은 KICK | 이전 deadline 경과 여부와 관계없이 새 deadline 인정 |
| STOP·스레드 종료 | 이후 검사에서 제외, TCB 해제 후 접근 없음 |
| 종료·재등록·TCB/PID 재사용 | 이전 등록 정보가 새 스레드에 영향을 주지 않음 |
| 미등록 KICK·중복 START·미등록 STOP | 정한 반환 정책과 무동작 정책 준수 |
| SMP 동시 START·KICK·STOP | 힙과 TCB 상태 일관성, 교착 없음 |
| ISR 잠금 경합 | ISR이 잠금을 기다리지 않고 다음 tick에 재시도 |
| 여러 스레드의 fd 공유 | 각 ioctl이 fd 소유자가 아닌 호출 스레드 자신에게 적용 |
| 동일하거나 이미 지난 검사 예약 시각 다수 | 도래한 후보를 모두 확인·재정렬, 실제 만료 대상을 누락하지 않음 |
| 정상 sleep·복귀 | sleep 경과 시간 포함, 최소 검사 예약에 맞춘 wakeup, 동일한 늦은 KICK 정책 적용 |
| HW WDT | 실제 시작, tick keepalive 중단 시 리셋, 정상 sleep 중 오동작 여부 확인 |
| reboot reason·PANIC | 신규 SW reason과 기존 진단·리셋 경로 동작 확인 |
| 시간 경계 | 지원 timeout 범위, tick 변환과 wraparound 비교 확인 |

성능은 평상시 tick, KICK, 최대 256개 후보 동시 도래, 다른 CPU의 잠금 대기시간을 측정한다. 보드의 누락 tick 반복 처리도 포함한다. 고정된 100ms 합격 기준은 두지 않으며, 측정 결과가 실제 시스템 운용에 적절한지 확인한다.

후보 개수 제한, 별도 worker, 추가 진단 기능은 측정 전에 선제적으로 추가하지 않는다. 현재 범위는 등록 스레드의 deadline 감시를 가장 단순한 구조로 완성하는 것이다.
