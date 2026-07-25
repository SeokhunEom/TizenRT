# 스케줄러 작업 가이드

이 문서는 TizenRT scheduler가 task와 pthread를 어떤 자료구조로 관리하고, 생성/실행/대기/깨움/priority 변경/종료 시 어떤 경로로 context switch를 일으키는지 설명한다. 기본 설명은 단일 CPU(non-SMP) 흐름을 기준으로 하고, SMP 차이는 별도 섹션에서 정리한다.

## 핵심 요약

- scheduler의 기본 단위는 `struct tcb_s`다. task와 pthread는 각각 다른 확장 TCB를 쓰지만 공통 scheduler 필드는 `struct tcb_s`에 있다.
- ready-to-run list는 priority 순서로 정렬된다. TizenRT에서는 priority 숫자가 클수록 먼저 실행된다.
- 단일 CPU에서 `g_readytorun.head`는 현재 실행 중인 task다. tail은 항상 IDLE task다.
- `sched_lock()`은 interrupt를 끄지 않고 preemption만 막는다. 이때 실행 가능해진 더 높은 priority task는 `g_pendingtasks`에 쌓였다가 `sched_unlock()` 때 release된다.
- scheduler 공통 코드는 task list와 state를 바꾸고 "head가 바뀌었는지"를 판단한다. 실제 register save/restore와 context switch는 architecture 함수인 `up_block_task()`, `up_unblock_task()`, `up_release_pending()`, `up_reprioritize_rtr()`가 수행한다.
- `SCHED_FIFO`는 높은 priority task가 준비되면 선점하고, 같은 priority끼리는 명시적 yield/block/priority 변경이 없으면 계속 실행된다.
- `SCHED_RR`은 `CONFIG_RR_INTERVAL > 0`일 때 활성화되며, 같은 priority task 사이에서 time slice가 끝나면 뒤로 밀어 CPU를 양보한다.

## 주요 파일

| 관심사 | 파일 |
|---|---|
| 공개 스케줄러 API | `os/include/sched.h` |
| TCB, 작업 상태, 작업 그룹 | `os/include/tinyara/sched.h` |
| scheduler 내부 선언 | `os/kernel/sched/sched.h` |
| task list 전역 변수와 초기화 | `os/kernel/init/os_start.c` |
| scheduler source 목록 | `os/kernel/sched/Make.defs` |
| priority list 삽입 | `os/kernel/sched/sched_addprioritized.c` |
| ready list 진입/이탈 | `os/kernel/sched/sched_addreadytorun.c`, `os/kernel/sched/sched_removereadytorun.c` |
| blocked list 진입/이탈 | `os/kernel/sched/sched_addblocked.c`, `os/kernel/sched/sched_removeblocked.c` |
| 릴리스 보류 중 | `os/kernel/sched/sched_mergepending.c` |
| 선점 잠금 | `os/kernel/sched/sched_lock.c`, `os/kernel/sched/sched_unlock.c` |
| priority/policy 변경 | `os/kernel/sched/sched_setparam.c`, `os/kernel/sched/sched_setpriority.c`, `os/kernel/sched/sched_setscheduler.c`, `os/kernel/sched/sched_reprioritize.c` |
| 수익률 | `os/kernel/sched/sched_yield.c` |
| 타이머 틱 / RR | `os/kernel/sched/sched_processtimer.c`, `os/kernel/sched/sched_timerexpiration.c` |
| TCB 조회 | `os/kernel/sched/sched_gettcb.c` |에 대한 PID
| task 생성 후 scheduler setup | `os/kernel/task/task_setup.c`, `os/kernel/task/task_activate.c` |
| ARMv8-M context switch 예시 | `os/arch/arm/src/armv8-m/up_blocktask.c`, `os/arch/arm/src/armv8-m/up_unblocktask.c`, `os/arch/arm/src/armv8-m/up_releasepending.c` |

## 기본 용어

### TCB

`struct tcb_s`는 scheduler가 직접 보는 공통 실행 단위다. 중요한 필드는 다음과 같다.

| 필드 | 의미 |
|---|---|
| `flink`, `blink` | scheduler list 연결 |
| `pid` | task/pthread ID |
| `start`, `entry` | kernel start wrapper와 user entry |
| `sched_priority` | 현재 priority |
| `base_priority`, `boost_priority`, `holdsem` | priority inheritance가 켜졌을 때 사용하는 priority/held semaphore 정보 |
| `task_state` | 현재 TCB가 어느 list에 있는지 나타내는 상태 |
| `flags` | task/pthread/kernel thread 타입, RR policy, cancellation 등 |
| `lockcount` | `sched_lock()` 중첩 수 |
| `cpu`, `affinity` | SMP에서 CPU 배정과 affinity |
| `timeslice` | RR policy에서 남은 time slice |
| `waitsem` | semaphore wait 중인 경우 대상 semaphore |
| `xcp` | 아키텍처 레지스터 저장 영역 |

task는 `struct task_tcb_s`, pthread는 `struct pthread_tcb_s`를 사용하지만 두 구조체의 첫 필드는 공통 `struct tcb_s`다. 그래서 scheduler는 둘을 같은 방식으로 list에 넣고 뺄 수 있다.

### 우선순위

priority 범위는 `os/include/sys/types.h`에 있다.

```c
#define SCHED_PRIORITY_MIN       1
#define SCHED_PRIORITY_DEFAULT 100
#define SCHED_PRIORITY_MAX     255
```

숫자가 클수록 높은 priority다. `sched_addprioritized()`는 list를 priority 내림차순으로 유지한다. 같은 priority가 이미 있으면 새 TCB는 같은 priority 그룹의 뒤에 들어간다. 그래서 같은 priority에서 yield나 RR rotation이 가능하다.

### 작업 상태

`enum tstate_e`는 TCB가 어떤 scheduler list에 속하는지 나타낸다.

| state | 의미 | 대표 list |
|---|---|---|
| `TSTATE_TASK_INVALID` | 아직 유효한 list에 없음 | 없음 |
| `TSTATE_TASK_PENDING` | preemption lock 때문에 ready로 못 감 | `g_pendingtasks` |
| `TSTATE_TASK_READYTORUN` | 실행 가능하지만 현재 실행 중은 아님 | `g_readytorun` |
| `TSTATE_TASK_ASSIGNED` | SMP에서 특정 CPU에 배정됨 | `g_assignedtasks[cpu]` |
| `TSTATE_TASK_RUNNING` | 현재 실행 중 | non-SMP: `g_readytorun.head`, SMP: `g_assignedtasks[cpu].head` |
| `TSTATE_TASK_INACTIVE` | 초기화됐지만 activate 전 | `g_inactivetasks` |
| `TSTATE_WAIT_SEM` | semaphore 대기 | `g_waitingforsemaphore` |
| `TSTATE_WAIT_FIN` | FIN unblock 대기 | `g_waitingforfin` |
| `TSTATE_WAIT_SIG` | signal 대기 | `g_waitingforsignal` |
| `TSTATE_WAIT_MQNOTEMPTY` | message queue not-empty 대기 | `g_waitingformqnotempty` |
| `TSTATE_WAIT_MQNOTFULL` | message queue not-full 대기 | `g_waitingformqnotfull` |
| `TSTATE_WAIT_PAGEFILL` | page fill 대기 | `g_waitingforfill` |

`os/kernel/init/os_start.c`의 `g_tasklisttable[]`이 state와 list를 매핑한다. 이 테이블의 attribute는 해당 list가 priority 정렬인지, SMP에서 CPU index가 필요한지, running task를 포함하는지를 나타낸다.

## Task List 구조

### g_readytorun

단일 CPU에서 가장 중요한 list다.

- priority 순서로 정렬된다.
- head는 현재 실행 중인 task다.
- tail은 항상 IDLE task다.
- `current_task(0)` 또는 `this_task()`는 결국 `g_readytorun.head`를 가리킨다.

즉 non-SMP에서는 context switch란 "어떤 TCB가 `g_readytorun.head`가 되는가"를 바꾸는 일이다.

### g_pendingtasks

preemption이 잠긴 상태에서 더 높은 priority task가 ready 상태가 되면 바로 head를 바꿀 수 없다. 이때 새 task는 `g_pendingtasks`에 들어간다.

대표 상황:

1. 현재 running task가 `sched_lock()`으로 preemption을 막았다.
2. interrupt나 다른 kernel 경로에서 더 높은 priority task를 깨웠다.
3. `sched_addreadytorun()`은 현재 task를 선점시키지 않고 새 task를 `TSTATE_TASK_PENDING`으로 둔다.
4. 현재 task가 `sched_unlock()`을 호출해 `lockcount`가 0이 되면 `up_release_pending()`이 실행된다.
5. `up_release_pending()`은 `sched_mergepending()`으로 pending list를 ready list에 합친다.
6. ready list head가 바뀌면 context switch가 발생한다.

### 차단 목록

blocked list는 wait 이유별로 나뉜다.

- 세마포어: `g_waitingforsemaphore`
- 신호: `g_waitingforsignal`
- 메시지 대기열: `g_waitingformqnotempty`, `g_waitingformqnotfull`
- FIN: `g_waitingforfin`
- 페이지 채우기: `g_waitingforfill`

`sched_addblocked()`는 TCB를 해당 blocked list에 넣고 state를 갱신한다. `sched_removeblocked()`는 해당 list에서 제거하고 state를 `TSTATE_TASK_INVALID`로 돌린다.

### g_inactivetasks

생성은 됐지만 아직 scheduler가 실행 대상으로 삼지 않는 TCB가 들어간다. `thread_schedsetup()`은 TCB 초기화가 끝나면 `g_inactivetasks`에 넣고 state를 `TSTATE_TASK_INACTIVE`로 둔다. 이후 `task_activate()`가 `up_unblock_task()`를 호출하면 ready/running 쪽으로 이동한다.

## 부팅과 초기화

`os_start()`는 scheduler 전역 list를 초기화하고 IDLE task TCB를 만든다.

1. `g_readytorun`, `g_pendingtasks`, wait list, `g_inactivetasks`를 초기화한다.
2. CPU별 IDLE TCB를 0으로 초기화한다.
3. IDLE task는 `pid == 0`, `sched_priority == 0`을 가진다.
4. IDLE task state는 `TSTATE_TASK_RUNNING`으로 시작한다.
5. 이후 user initialization task가 만들어지고, 시스템 나머지 초기화가 진행된다.

IDLE task priority가 0이고 일반 priority 최소값이 1인 이유는 ready task가 없을 때 항상 마지막 실행 대상으로 남기기 위해서다.

## Task 생성에서 실행까지

task와 pthread 모두 scheduler 관점에서는 비슷한 단계로 들어온다.

```text
TCB allocation
  -> thread_schedsetup()
  -> g_inactivetasks / TSTATE_TASK_INACTIVE
  -> task_activate()
  -> up_unblock_task()
  -> sched_removeblocked()
  -> sched_addreadytorun()
  -> ready or running
```

핵심 함수는 `thread_schedsetup()`이다.

- priority 범위를 검사한다.
- `task_assignpid()`로 PID를 할당하고 `g_pidhash[]`에 TCB를 등록한다.
- `sched_priority`, `start`, `entry`, thread type flag를 저장한다.
- `CONFIG_RR_INTERVAL > 0`이면 초기 policy를 RR로 설정하고 `timeslice`를 채운다.
- parent/affinity/signal mask 등 필요한 속성을 상속한다.
- architecture별 초기 context를 `up_initial_state()`로 만든다.
- TCB를 `g_inactivetasks`에 넣고 `TSTATE_TASK_INACTIVE`로 둔다.

`task_activate()`는 signal handler 등 activation 부가 작업을 한 뒤 `up_unblock_task(tcb)`를 호출한다. `up_unblock_task()`가 TCB를 inactive/blocked list에서 제거하고 ready list에 넣는다. 이때 새 TCB가 ready list head가 되면 곧바로 context switch가 가능하다.

## Ready List 삽입 규칙

`sched_addreadytorun()`은 TCB를 ready 상태로 만들 때 호출된다. non-SMP 기준 흐름은 다음과 같다.

1. 현재 running TCB를 `rtcb = this_task()`로 얻는다.
2. 현재 task가 `sched_lock()` 상태이고, 새 TCB priority가 현재 task보다 높으면 새 TCB를 `g_pendingtasks`에 넣는다.
3. 그렇지 않으면 `sched_addprioritized()`로 `g_readytorun`에 넣는다.
4. 새 TCB가 list head가 되면 새 TCB state는 `TSTATE_TASK_RUNNING`, 기존 head는 `TSTATE_TASK_READYTORUN`이 된다.
5. head가 바뀌었으면 `true`를 반환한다.

`sched_addreadytorun()` 자체가 context switch를 직접 수행하지 않는다. caller가 반환값을 보고 architecture switch 함수를 호출한다.

## Running Task가 Block될 때

예를 들어 semaphore를 기다리는 흐름은 다음과 같다.

```text
sem_wait()
  -> semcount <= 0
  -> rtcb->waitsem = sem
  -> up_block_task(rtcb, TSTATE_WAIT_SEM)
  -> sched_removereadytorun(rtcb)
  -> sched_addblocked(rtcb, TSTATE_WAIT_SEM)
  -> sched_mergepending() if needed
  -> context switch if ready list head changed
```

`up_block_task()`는 architecture 계층 함수다. 공통 scheduler helper를 사용해 list를 바꾼 뒤, switch가 필요하면 register context를 저장하고 새 head TCB의 context를 복구한다.

interrupt context에서는 `current_regs`를 이용해 현재 exception context를 저장/복원한다. thread context에서는 `up_switchcontext(old_regs, new_regs)`로 전환한다.

## Blocked Task가 깨어날 때

semaphore release 예시는 다음과 같다.

```text
sem_post()
  -> semcount++
  -> waiting task 검색
  -> up_unblock_task(stcb)
  -> sched_removeblocked(stcb)
  -> timeslice reset
  -> sched_addreadytorun(stcb)
  -> context switch if ready list head changed
```

`sem_post()`는 interrupt handler에서도 호출될 수 있으므로 critical section 안에서 동작한다. semaphore wait list는 priority 정렬 list이기 때문에, 같은 semaphore를 기다리는 task 중 가장 높은 priority task를 먼저 깨운다.

## 선점 잠금

`sched_lock()`과 `sched_unlock()`은 scheduler preemption을 막고 푸는 API다. interrupt disable과 다르다.

### sched_lock()

non-SMP에서는 현재 TCB의 `lockcount`를 증가시킨다.

```text
sched_lock()
  -> this_task()
  -> if not interrupt context
  -> rtcb->lockcount++
```

이 상태에서 더 높은 priority task가 ready가 되어도 `g_readytorun.head`를 바꾸지 않는다. 대신 `g_pendingtasks`에 넣는다.

### sched_unlock()

`sched_unlock()`은 `lockcount`를 감소시키고, 0이 되면 pending task를 release한다.

```text
sched_unlock()
  -> rtcb->lockcount--
  -> if lockcount == 0 and g_pendingtasks not empty
  -> up_release_pending()
  -> sched_mergepending()
  -> context switch if head changed
```

`sched_lock()`은 nesting 가능하다. `sched_unlock()`이 같은 횟수만큼 호출되어 `lockcount`가 0이 되어야 preemption이 다시 가능하다.

## Context Switch 경계

TizenRT scheduler는 공통 로직과 architecture 로직을 분리한다.

공통 scheduler helper:

- `sched_addreadytorun()`
- `sched_removereadytorun()`
- `sched_addblocked()`
- `sched_removeblocked()`
- `sched_mergepending()`
- `sched_setpriority()`

architecture switch 함수:

- `up_block_task()`
- `up_unblock_task()`
- `up_release_pending()`
- `up_reprioritize_rtr()`
- `up_switchcontext()`
- `up_savestate()`
- `up_restorestate()`
- `up_restoretask()`

공통 helper는 list와 state를 바꾸고 boolean으로 "현재 active task가 바뀌었는지"를 알려준다. architecture 함수는 그 결과에 따라 실제 CPU register context를 저장/복원한다.

## 스케줄링 정책

### SCHED_FIFO

`SCHED_FIFO`는 priority 기반 선점 정책이다.

- 더 높은 priority task가 ready가 되면 현재 task를 선점할 수 있다.
- 같은 priority task는 자동 time slice rotation이 없다.
- 같은 priority에서 CPU를 넘기려면 `sched_yield()`, blocking wait, priority 변경, 종료 같은 이벤트가 필요하다.

`CONFIG_RR_INTERVAL == 0`이면 `sched_setscheduler()`는 `SCHED_FIFO`만 허용한다.

### SCHED_RR

`SCHED_RR`은 `CONFIG_RR_INTERVAL > 0`일 때 사용할 수 있다.

- `sched_setscheduler(pid, SCHED_RR, param)`은 TCB에 `TCB_FLAG_ROUND_ROBIN`을 설정한다.
- `timeslice`는 `MSEC2TICK(CONFIG_RR_INTERVAL)`로 초기화된다.
- timer tick마다 `sched_process_timer()`가 호출되고, 내부에서 `sched_process_scheduler()`가 RR time slice를 처리한다.
- 현재 RR task의 `timeslice`가 끝났고 다음 ready task가 같은 priority 이상이면 `up_reprioritize_rtr(rtcb, rtcb->sched_priority)`로 현재 task를 같은 priority 그룹 뒤로 보낸다.
- 현재 task가 `sched_lock()` 상태면 time slice 만료가 즉시 switch되지 않고, unlock 시 재평가된다.

RR은 priority가 다른 task 사이의 공정성을 보장하는 정책이 아니다. 같은 priority 그룹 안에서만 rotation을 제공한다.

## sched_yield()

`sched_yield()`는 현재 task가 같은 priority의 다른 ready task에게 CPU를 넘기기 위한 API다.

기본 구현은 현재 priority를 다시 설정하는 방식이다.

```text
sched_yield()
  -> sched_setpriority(rtcb, rtcb->sched_priority)
```

`sched_setpriority()`는 같은 priority로 재설정할 때 TCB를 같은 priority 그룹 뒤쪽으로 다시 넣는다. 그래서 같은 priority의 다른 task가 있으면 그 task가 먼저 실행될 수 있다.

`CONFIG_SCHED_YIELD_OPTIMIZATION`이 켜져 있으면 architecture 최적화 함수 `up_schedyield()`를 사용한다.

## Priority 변경

public API는 `sched_setparam()`과 `sched_setscheduler()`다. 내부적으로는 `sched_reprioritize()` 또는 `sched_setpriority()`로 이어진다.

### sched_setparam()

```text
sched_setparam(pid, param)
  -> sched_lock()
  -> pid가 0이면 current task
  -> sched_gettcb(pid)
  -> sched_reprioritize(tcb, param->sched_priority)
  -> sched_unlock()
```

`CONFIG_PRIORITY_INHERITANCE`가 켜져 있으면 `sched_reprioritize()`는 `sched_setpriority()` 후 `base_priority`와 `boost_priority`도 정리한다. 즉 명시적 priority 변경은 기존 priority inheritance boost 이력을 버리는 의미가 있다.

### sched_setpriority()

`sched_setpriority()`는 대상 TCB의 현재 state에 따라 다르게 처리한다.

- running task의 priority가 낮아져 다음 ready task가 우선될 수 있으면 `up_reprioritize_rtr()`로 context switch를 유발한다.
- ready-to-run task의 priority가 올라 현재 running task보다 높아지면 역시 `up_reprioritize_rtr()`가 필요하다.
- prioritized blocked list에 있는 task는 list에서 뺐다가 새 priority 위치에 다시 넣는다.
- non-prioritized blocked list에 있는 task는 priority 값만 바꾼다.

## Timer Tick 흐름

non-tickless 빌드에서 timer interrupt는 architecture timer code가 주기적으로 `sched_process_timer()`를 호출하는 구조다.

```text
sched_process_timer()
  -> clock_timer()
  -> sched_process_cpuload() if enabled
  -> sched_process_scheduler()
     -> RR timeslice 처리
  -> wd_timer()
```

`CONFIG_SCHED_TICKLESS`가 켜져 있으면 `sched_timerexpiration.c` 경로를 사용한다. tickless에서도 핵심 의미는 같다. 경과한 tick 수를 계산해 clock/watchdog/RR time slice를 처리하고 다음 timer interval을 재설정한다.

## Semaphore와 Priority Inheritance

semaphore wait/post는 scheduler와 밀접하게 연결되어 있다.

`sem_wait()`에서 semaphore를 바로 얻지 못하면:

1. `sem->semcount`를 감소시킨다.
2. `rtcb->waitsem`에 기다리는 semaphore를 저장한다.
3. priority inheritance가 켜져 있으면 `sched_lock()`으로 scheduler 변경을 잠시 막는다.
4. holder priority를 `sem_boostpriority()`로 올릴 수 있다.
5. `up_block_task(rtcb, TSTATE_WAIT_SEM)`으로 현재 task를 semaphore wait list로 보낸다.
6. 깨어난 뒤 정상 획득인지 signal/timeout인지 `errno`로 구분한다.

`sem_post()`에서 waiter가 있으면:

1. `g_waitingforsemaphore`에서 같은 semaphore를 기다리는 가장 높은 priority TCB를 찾는다.
2. 그 TCB를 semaphore holder로 등록한다.
3. `up_unblock_task(stcb)`로 깨운다.
4. priority inheritance가 켜져 있으면 holder priority를 `sem_restorebaseprio()`로 복원한다.
5. `sched_unlock()`으로 잠겨 있던 scheduler를 풀어 pending switch를 허용한다.

이 흐름 때문에 semaphore 관련 수정은 scheduler list 순서, priority inheritance 복원, pending release 순서를 함께 봐야 한다.

## PID Hash와 조회

TizenRT는 `CONFIG_MAX_TASKS` 크기의 `g_pidhash[]`를 사용해 PID를 TCB로 빠르게 찾는다.

- `task_assignpid()`는 `g_lastpid`를 증가시키며 빈 hash slot을 찾는다.
- hash index는 `PIDHASH(pid)`로 계산한다.
- 빈 slot을 찾으면 `g_pidhash[hash_ndx].tcb = tcb`, `g_pidhash[hash_ndx].pid = next_pid`를 저장한다.
- `g_alive_taskcount`를 증가시킨다.
- `sched_gettcb(pid)`는 같은 hash index에서 PID가 일치하는지 확인한 뒤 TCB를 반환한다.

`CONFIG_MAX_TASKS`는 power of 2여야 한다. scheduler 내부에서 PID hash와 task 개수 제한이 이 전제에 의존한다.

## SMP 차이

SMP에서는 ready list 의미가 달라진다.

비SMP:

```text
g_readytorun
  head = 현재 실행 task
  tail = IDLE task
```

SMP:

```text
g_readytorun
  CPU에 아직 배정되지 않은 ready task

g_assignedtasks[cpu]
  head = 해당 CPU에서 현재 실행 task
  tail = 해당 CPU의 IDLE task
```

SMP에서 task는 다음 중 하나로 CPU에 배정된다.

- affinity API나 pthread attribute로 특정 CPU에 고정된다.
- scheduler가 실행 시점에 임시로 CPU를 선택한다.

`sched_addreadytorun()`은 새 TCB의 affinity와 CPU lock 상태를 보고 어느 CPU에 넣을지 결정한다. 다른 CPU의 assigned list를 수정해야 하면 `up_cpu_pause(cpu)`로 해당 CPU를 멈춘 뒤 list를 수정하고 `up_cpu_resume(cpu)`로 재개한다.

SMP preemption lock은 단순 `lockcount`만으로 부족하다. 다른 CPU는 계속 실행될 수 있기 때문이다. 그래서 다음 전역 상태를 함께 사용한다.

- `g_cpu_schedlock`
- `g_cpu_lockset`
- `g_cpu_locksetlock`
- `g_cpu_tasklistlock`

SMP에서 pending release는 다른 CPU의 scheduler lock이나 IRQ critical section 상태도 고려한다. 조건이 안전하지 않으면 pending task는 계속 `g_pendingtasks`에 남는다.

## 주요 상태 전이

### 생성

```text
TSTATE_TASK_INVALID
  -> TSTATE_TASK_INACTIVE
  -> TSTATE_TASK_READYTORUN or TSTATE_TASK_RUNNING
```

경로:

```text
task_create()/pthread_create()
  -> task_schedsetup()/pthread_schedsetup()
  -> thread_schedsetup()
  -> task_activate()
  -> up_unblock_task()
```

### block

```text
TSTATE_TASK_RUNNING
  -> TSTATE_WAIT_SEM / TSTATE_WAIT_SIG / TSTATE_WAIT_MQ...
```

경로:

```text
wait API
  -> up_block_task()
  -> sched_removereadytorun()
  -> sched_addblocked()
```

### 차단 해제

```text
TSTATE_WAIT_*
  -> TSTATE_TASK_READYTORUN or TSTATE_TASK_RUNNING or TSTATE_TASK_PENDING
```

경로:

```text
signal/post/message/timeout
  -> up_unblock_task()
  -> sched_removeblocked()
  -> sched_addreadytorun()
```

### 선점 잠긴 웨이크업

```text
TSTATE_WAIT_*
  -> TSTATE_TASK_PENDING
  -> TSTATE_TASK_READYTORUN or TSTATE_TASK_RUNNING
```

경로:

```text
up_unblock_task()
  -> sched_addreadytorun()
  -> g_pendingtasks
  -> sched_unlock()
  -> up_release_pending()
  -> sched_mergepending()
```

### exit

```text
TSTATE_TASK_RUNNING
  -> removed from ready list
  -> next ready task becomes running
  -> TCB release
```

경로:

```text
exit()/pthread_exit()/task_delete()
  -> task_exit()/task_terminate()
  -> sched_removereadytorun()
  -> sched_releasetcb()
```

## Scheduler 수정 시 지켜야 할 불변식

- `task_state`와 실제 list 위치는 항상 일치해야 한다.
- ready/pending/semaphore wait/message wait처럼 prioritized list인 경우 priority 순서를 깨면 안 된다.
- non-SMP에서 `g_readytorun.head`는 running task여야 하고 tail에는 IDLE task가 남아야 한다.
- `sched_addreadytorun()`이나 `sched_removereadytorun()`을 호출하는 쪽은 head 변경 반환값을 보고 context switch 필요성을 처리해야 한다.
- scheduler list를 바꾸는 경로는 critical section 안에서 실행되어야 한다.
- `sched_lock()`은 interrupt lock이 아니다. interrupt handler와 공유되는 자료구조는 별도 critical section이 필요하다.
- `sched_lock()` 중 더 높은 priority task를 ready list head로 직접 올리면 안 된다. pending list를 통해 unlock 시점에 release해야 한다.
- priority inheritance 경로에서는 holder boost와 restore 사이에 waiter를 먼저 실행시키지 않도록 순서를 유지해야 한다.
- 같은 priority의 순서를 바꾸는 동작은 side effect가 크다. `sched_yield()`와 RR이 이 순서에 의존한다.
- SMP에서는 `g_readytorun`과 `g_assignedtasks[cpu]`의 의미가 다르므로 non-SMP 가정으로 list를 직접 조작하면 안 된다.

## 디버깅할 때 보는 순서

스케줄러 문제를 볼 때는 아래 순서로 좁히는 것이 좋다.

1. 대상 TCB의 `pid`, `task_state`, `sched_priority`, `flags`, `lockcount`, `waitsem`을 확인한다.
2. `task_state`가 가리키는 list에 실제로 들어 있는지 확인한다.
3. ready/pending/wait list의 priority 순서가 맞는지 확인한다.
4. `sched_lock()`이 풀리지 않아 `g_pendingtasks`에 갇힌 상태인지 확인한다.
5. semaphore 문제라면 `waitsem`, semaphore holder, priority inheritance boost/restore를 같이 확인한다.
6. RR 문제라면 `CONFIG_RR_INTERVAL`, `TCB_FLAG_ROUND_ROBIN`, `timeslice`, 같은 priority의 다음 ready task를 확인한다.
7. context switch가 안 되면 공통 scheduler helper 반환값과 architecture `up_*` 함수 호출 여부를 나눠서 본다.
8. SMP 문제라면 affinity, `TCB_FLAG_CPU_LOCKED`, `cpu`, `g_assignedtasks[cpu]`, global scheduler lock 상태를 같이 본다.

## 변경 위치 선택 기준

| 바꾸려는 동작 | 먼저 볼 위치 |
|---|---|
| 우선주문 | `sched_addprioritized.c`, `sched_setpriority.c` |
| preemption lock / pending 처리 | `sched_lock.c`, `sched_unlock.c`, `sched_mergepending.c`, `sched_addreadytorun.c` |
| block/unblock 상태 전이 | `sched_addblocked.c`, `sched_removeblocked.c`, architecture `up_blocktask.c`, `up_unblocktask.c` |
| context switch 발생 조건 | `sched_addreadytorun.c`, `sched_removereadytorun.c`, `up_releasepending.c`, `up_reprioritize_rtr()` 구현 |
| RR 타임 슬라이스 | `sched_processtimer.c`, `sched_timerexpiration.c`, `sched_setscheduler.c` |
| 우선순위 상속 | `os/kernel/semaphore/sem_wait.c`, `os/kernel/semaphore/sem_post.c`, `os/kernel/semaphore/sem_holder.c`, `sched_reprioritize.c` |
| task 생성 초기 state | `task_setup.c`, `task_activate.c` |
| PID lookup 문제 | `task_setup.c`, `sched_gettcb.c`, `sched_releasetcb.c` |
| SMP CPU 선택/affinity | `sched_cpuselect.c`, `sched_setaffinity.c`, `sched_addreadytorun.c`, `sched_removereadytorun.c` |

## 짧은 예시

### 더 높은 priority task가 semaphore에서 깨어나는 경우

```text
현재 실행: low(priority 100)
대기 중: high(priority 200), TSTATE_WAIT_SEM

low가 sem_post()
  -> high를 g_waitingforsemaphore에서 제거
  -> high를 g_readytorun에 삽입
  -> high priority가 더 높으므로 head 변경
  -> up_unblock_task()가 low context 저장 후 high context 복원
```

### sched_lock 중 더 높은 priority task가 깨어나는 경우

```text
현재 실행: low(priority 100, lockcount 1)
대기 중: high(priority 200)

interrupt에서 high wakeup
  -> sched_addreadytorun(high)
  -> low lockcount > 0 and high priority > low priority
  -> high는 g_pendingtasks에 들어감
  -> low 계속 실행

low가 sched_unlock()
  -> lockcount 0
  -> up_release_pending()
  -> high가 g_readytorun.head
  -> context switch
```

### 같은 priority에서 sched_yield()

```text
ready list:
  A(priority 100, running)
  B(priority 100, ready)
  IDLE(priority 0)

A가 sched_yield()
  -> sched_setpriority(A, 100)
  -> A를 같은 priority 그룹 뒤로 이동
  -> B가 head
  -> context switch 가능
```

## 요약

TizenRT scheduler는 priority 정렬 list와 TCB state를 중심으로 동작한다. non-SMP에서는 `g_readytorun.head`가 곧 현재 실행 task이고, 선점 가능 여부는 priority와 `lockcount`가 결정한다. 실행 가능하지만 선점할 수 없는 task는 `g_pendingtasks`에 머물다가 unlock 시 release된다. 실제 context switch는 architecture `up_*` 함수가 수행하므로, 스케줄러를 수정할 때는 "list/state 변경"과 "register context 전환"의 책임 경계를 유지해야 한다.
