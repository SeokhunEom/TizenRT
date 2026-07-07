# Locking Mechanisms Guide

이 문서는 TizenRT의 lock 계층을 `sem_t`, `pthread_mutex_t`, `sched_lock()`, `irqsave()`, `enter_critical_section()`, `spinlock_t` 중심으로 정리한다. 기준 defconfig는 `build/configs/rtl8730e/loadable_ext_ddr_st7785/defconfig`이며, 이 설정은 SMP 2 CPU와 priority inheritance가 켜져 있으므로 단일 CPU 설명만으로는 충분하지 않다.

## 핵심 요약

- TizenRT lock은 크게 sleep 가능한 lock, preemption lock, interrupt/SMP lock으로 나뉜다.
- `sem_wait()`와 `pthread_mutex_lock()`은 task를 block시킬 수 있으므로 interrupt context나 spinlock/critical section 안에서 호출하면 안 된다.
- `sem_post()`는 interrupt handler에서도 호출될 수 있게 구현되어 있다. 그래서 `sem_wait()`/`sem_post()` 내부는 `enter_critical_section()`으로 보호된다.
- `sched_lock()`은 interrupt를 끄지 않는다. 현재 task의 preemption을 막고, ready가 된 더 높은 priority task를 pending으로 미루는 용도다.
- `irqsave()`는 architecture raw interrupt mask다. 일반 OS 코드에서는 가능한 `enter_critical_section()`/`leave_critical_section()`을 사용한다.
- `enter_critical_section()`은 `CONFIG_IRQCOUNT`와 `CONFIG_SMP`가 켜진 경우 단순 `irqsave()`가 아니다. local IRQ disable에 더해 CPU별 nesting count, 전역 IRQ spinlock, CPU set을 관리한다.
- `spin_lock()`은 busy-wait lock이다. sleep하지 못하는 짧은 SMP 공유 자료구조 보호에 쓰며, 같은 CPU에서 재진입하면 deadlock이 날 수 있다.
- `spin_lock_irqsave(NULL)`은 SMP에서 nesting 가능한 전역 IRQ spinlock을 사용한다. 반면 caller가 넘긴 `spinlock_t *lock`은 같은 lock으로 재진입하면 deadlock이 날 수 있다.
- Realtek rtl8730e wrapper의 `rtw_spin_lock()`은 이름과 달리 TizenRT `spinlock_t`가 아니라 `sem_t` 기반 sleep lock이다.

## 기준 defconfig

`rtl8730e/loadable_ext_ddr_st7785` 기준으로 lock 동작에 영향을 주는 주요 옵션은 다음과 같다.

| 설정 | 값 | 의미 |
|---|---:|---|
| `CONFIG_ARCH_ARM` | `y` | ARM architecture |
| `CONFIG_ARCH_CHIP_AMEBASMART` | `y` | rtl8730e chip family |
| `CONFIG_ARCH_ARMV7A_FAMILY` | `y` | ARMv7-A IRQ 구현 사용 |
| `CONFIG_ARCH_HAVE_TESTSET` | `y` | architecture atomic test-and-set 제공 |
| `CONFIG_ARM_HAVE_WFE_SEV` | `y` | spin wait/unlock에 `wfe`/`sev` 사용 가능 |
| `CONFIG_SPINLOCK` | `y` | kernel `spinlock_t` 활성화 |
| `CONFIG_IRQCOUNT` | `y` | `enter_critical_section()`이 TCB/CPU nesting count 관리 |
| `CONFIG_SMP` | `y` | SMP lock path 활성화 |
| `CONFIG_SMP_NCPUS` | `2` | CPU 2개 |
| `CONFIG_PRIORITY_INHERITANCE` | `y` | semaphore holder tracking과 priority boost 활성화 |
| `CONFIG_SEM_PREALLOCHOLDERS` | `16` | semaphore holder 구조체 preallocation |
| `CONFIG_PTHREAD_MUTEX_TYPES` | `y` | NORMAL/ERRORCHECK/RECURSIVE mutex type 지원 |
| `CONFIG_PTHREAD_MUTEX_UNSAFE` | `y` | robust mutex held-list 경로 비활성화 |
| `CONFIG_PTHREAD_MUTEX_ROBUST` | not set | robust mutex 기본 비활성화 |
| `CONFIG_BINARY_MANAGER` | `y` | semaphore holder/recovery 관련 flag가 일부 활성화 |
| `CONFIG_CANCELLATION_POINTS` | not set | cancellation point 경로는 컴파일 설정상 비활성 |

## 주요 파일

| 관심사 | 파일 |
|---|---|
| semaphore public API와 `sem_t` 구조 | `os/include/semaphore.h` |
| semaphore 내부 선언 | `os/kernel/semaphore/semaphore.h` |
| semaphore wait/post/try/timed/reset/destroy | `os/kernel/semaphore/sem_wait.c`, `sem_post.c`, `sem_trywait.c`, `sem_timedwait.c`, `sem_tickwait.c`, `sem_waitirq.c`, `sem_reset.c`, `sem_destroy.c` |
| semaphore holder와 priority inheritance | `os/kernel/semaphore/sem_holder.c`, `sem_setprotocol.c`, `sem_recover.c`, `sem_initialize.c` |
| pthread mutex public API/type | `os/include/pthread.h`, `os/include/sys/types.h` |
| pthread mutex 구현 | `os/kernel/pthread/pthread_mutexinit.c`, `pthread_mutexlock.c`, `pthread_mutextrylock.c`, `pthread_mutexunlock.c`, `pthread_mutexdestroy.c`, `pthread_mutex.c`, `pthread_initialize.c` |
| scheduler preemption lock | `os/kernel/sched/sched_lock.c`, `sched_unlock.c`, `sched_lockcount.c`, `sched_mergepending.c`, `os/kernel/sched/sched.h` |
| critical section public API | `os/include/tinyara/irq.h` |
| critical section 구현 | `os/kernel/irq/irq_csection.c` |
| raw ARM IRQ save/restore | `os/arch/arm/include/armv7-a/irq.h`, `os/arch/arm/include/types.h` |
| spinlock public API | `os/include/tinyara/spinlock.h`, `os/arch/arm/include/spinlock.h` |
| spinlock 구현 | `os/kernel/semaphore/spinlock.c`, `os/kernel/irq/irq_spinlock.c`, `os/arch/arm/src/armv7-a/arm_testset.S` |
| rtl8730e wrapper | `os/board/rtl8730e/src/component/os_dep/osdep_service_sem.c`, `osdep_service_mutex.c`, `osdep_service_critical.c` |

## Lock 계층 구조

```text
pthread_mutex_t
  -> pthread_mutex_lock/unlock/trylock
  -> pthread_sem_take/give
  -> sem_wait/sem_post
  -> scheduler block/unblock
  -> enter_critical_section for list/count protection

sem_t
  -> sem_wait/sem_trywait/sem_timedwait
  -> sem_post/sem_waitirq/sem_recover
  -> holder tracking / priority inheritance
  -> TSTATE_WAIT_SEM, g_waitingforsemaphore

sched_lock
  -> current TCB lockcount
  -> SMP: g_cpu_schedlock, g_cpu_lockset
  -> g_pendingtasks release deferral

enter_critical_section
  -> irqsave
  -> SMP: g_cpu_irqlock, g_cpu_irqset, g_cpu_nestcount
  -> scheduler pending release coordination

spin_lock / spin_lock_irqsave
  -> spinlock_t busy wait
  -> ARM: up_testset by LDREXB/STREXB
  -> optional local IRQ disable/restore
```

## 어떤 lock을 선택할지

| 필요 | 사용할 것 | 이유 |
|---|---|---|
| task끼리 긴 구간을 상호 배제하고 block 가능해야 함 | `pthread_mutex_t` | owner, recursive/errorcheck type, priority inheritance policy를 제공한다. |
| count 기반 resource pool이나 event signaling | `sem_t` | count가 자원 수 또는 event 수를 표현한다. `sem_post()`는 ISR에서도 가능하다. |
| task context에서 아주 짧게 preemption만 막음 | `sched_lock()` | interrupt는 허용하면서 scheduler 선점만 지연한다. |
| scheduler list, semaphore count처럼 ISR과 경쟁하는 짧은 kernel 자료구조 보호 | `enter_critical_section()` | local IRQ disable과 SMP IRQ lock을 함께 처리한다. |
| architecture register read-modify-write 또는 SMP shared flag를 짧게 보호 | `spin_lock_irqsave()` | sleep 없이 local IRQ와 spinlock을 같이 잡는다. |
| 순수 SMP shared data를 busy-wait로 보호 | `spin_lock()` | atomic test-and-set 기반이다. interrupt context 재진입 가능성이 있으면 `_irqsave` 계열을 고려한다. |
| raw architecture IRQ 상태만 저장/복구 | `irqsave()`/`irqrestore()` | arch 내부나 아주 낮은 계층에서만 직접 쓴다. 일반 kernel code는 `enter_critical_section()`이 우선이다. |

## `irqsave()`와 `irqrestore()`

`irqsave()`는 architecture별 raw interrupt state 저장/disable 함수다. ARMv7-A 기준 구현은 `os/arch/arm/include/armv7-a/irq.h`에 있다.

- `irqstate_t`는 ARM non-thumb2에서 `unsigned int`다.
- `irqsave()`는 CPSR을 읽어 반환하고 `cpsid i`로 IRQ를 disable한다.
- `CONFIG_ARMV7A_DECODEFIQ`가 켜지면 FIQ도 disable한다. rtl8730e defconfig에서는 이 옵션이 꺼져 있다.
- `irqrestore(flags)`는 저장된 CPSR control field를 복구한다.

중요한 점은 `irqsave()` 자체는 scheduler의 `irqcount`, SMP 전역 IRQ lock, pending task release와 같은 OS 상태를 모른다는 것이다. 그래서 `os/include/tinyara/irq.h`도 일반적으로 wrapper인 `enter_critical_section()`과 `leave_critical_section()`을 쓰라고 유도한다.

`CONFIG_IRQCOUNT`가 꺼져 있으면 다음처럼 축약된다.

```c
#define enter_critical_section() irqsave()
#define leave_critical_section(f) irqrestore(f)
```

그러나 rtl8730e 기준은 `CONFIG_IRQCOUNT=y`, `CONFIG_SMP=y`이므로 실제 구현은 `os/kernel/irq/irq_csection.c`를 따른다.

## `enter_critical_section()`

`enter_critical_section()`은 kernel의 짧은 atomic section을 위한 기본 primitive다. `sem_wait()`, `sem_post()`, scheduler list 조작, watchdog, timer, binary manager 등 많은 kernel 경로가 이 API를 사용한다.

### non-SMP 동작

SMP가 아니면 흐름은 비교적 단순하다.

1. `irqsave()`로 local IRQ를 disable하고 이전 interrupt state를 저장한다.
2. interrupt context가 아니고 task list 초기화가 끝났으면 현재 TCB의 `irqcount`를 증가시킨다.
3. `leave_critical_section(flags)`에서 `irqcount`를 감소시킨다.
4. nesting이 끝나면 instrumentation note를 남기고 `irqrestore(flags)`로 이전 IRQ state를 복구한다.

### SMP 동작

rtl8730e 기준 경로는 SMP 동작이다. 관련 전역 변수는 다음과 같다.

| 변수 | 의미 |
|---|---|
| `g_cpu_irqlock` | critical section 전역 spinlock |
| `g_cpu_irqsetlock` | `g_cpu_irqset` 수정 보호용 spinlock |
| `g_cpu_irqset` | 어떤 CPU가 IRQ critical section을 들고 있는지 나타내는 bitset |
| `g_cpu_nestcount[]` | interrupt handler 안에서 nested critical section을 추적 |
| `tcb->irqcount` | task context의 critical section nesting count |

task context에서 처음 진입할 때 흐름은 다음과 같다.

```text
enter_critical_section()
  -> irqsave()
  -> current_task(cpu)
  -> tcb->irqcount == 0이면 g_cpu_irqlock 획득 대기
  -> spin_setbit(g_cpu_irqset, cpu, ...)
  -> tcb->irqcount = 1
  -> flags 반환
```

이미 같은 task가 critical section 안이면 `irqcount`만 증가한다. 이 nesting 때문에 여러 계층에서 `enter_critical_section()`을 중첩 호출해도 마지막 `leave_critical_section()`까지 IRQ state가 유지된다.

interrupt context에서 진입할 때는 `tcb->irqcount` 대신 `g_cpu_nestcount[cpu]`가 중요하다. interrupt handler는 이미 실행 중인 task와 별도로 재진입할 수 있으므로, 같은 interrupt handler 안의 nested call은 `g_cpu_nestcount`만 증가시킨다.

SMP 구현에는 deadlock 회피 로직도 있다. `irq_waitlock()`은 `g_cpu_irqlock`을 기다리며 spin하는 중에 CPU pause/hotplug 요청이 pending인지 검사한다. 다른 CPU가 이 CPU를 pause시키려고 하는데 이 CPU가 IRQ를 끈 채 spin만 하면 pause interrupt를 처리하지 못해 deadlock이 생길 수 있기 때문이다. 이 경우 잠시 빠져나와 pause 요청을 처리한 뒤 다시 lock 획득을 시도한다.

`leave_critical_section(flags)`의 마지막 진출 흐름은 다음과 같다.

```text
leave_critical_section(flags)
  -> nesting count 감소
  -> 마지막 nesting이면 g_cpu_irqset bit clear
  -> 마지막 CPU가 IRQ lock을 놓는 순간 pending task release 가능 여부 검사
  -> irqrestore(flags)
```

pending task release는 `g_pendingtasks`가 비어 있지 않고 scheduler global lock이 없을 때만 수행된다. SMP에서는 `sched_unlock()`과 critical section이 서로 pending task release 시점을 조율한다.

### 사용 규칙

- critical section 안에서는 block 가능한 함수를 호출하지 않는다.
- `sem_wait()`, `pthread_mutex_lock()`, memory allocation 중 sleep 가능성이 있는 경로는 critical section 밖에서 호출한다.
- critical section은 scheduler list, wait list, reference count, interrupt와 공유되는 flag처럼 매우 짧은 자료구조 변경에만 사용한다.
- `flags`는 반드시 같은 nesting level의 `leave_critical_section(flags)`에 전달한다.
- raw `irqrestore()`로 `enter_critical_section()`의 flags를 복구하지 않는다. `irqcount`/SMP lock 상태가 깨진다.

## `sched_lock()`과 `sched_unlock()`

`sched_lock()`은 preemption lock이다. interrupt disable이 아니다.

단일 CPU에서는 현재 TCB의 `lockcount`를 증가시키는 것이 핵심이다. `lockcount > 0`이면 더 높은 priority task가 ready가 되어도 즉시 현재 task를 선점하지 못하고 `g_pendingtasks`에 머문다.

SMP에서는 preemption lock만으로 다른 CPU 실행을 막을 수 없다. 그래서 추가 전역 상태를 쓴다.

| 변수 | 의미 |
|---|---|
| `tcb->lockcount` | 현재 task의 nested scheduler lock count |
| `g_cpu_schedlock` | 어떤 CPU라도 scheduler lock을 들고 있으면 locked |
| `g_cpu_lockset` | scheduler lock을 들고 있는 CPU bitset |
| `g_cpu_locksetlock` | `g_cpu_lockset` 수정 보호 |

`sched_lock()` SMP 흐름은 다음과 같다.

```text
sched_lock()
  -> interrupt context면 효과 없음
  -> enter_critical_section()
  -> lockcount가 0이면 g_cpu_lockset에 this_cpu bit set
  -> tcb->lockcount++
  -> ready-to-run 후보를 pending list로 이동
  -> leave_critical_section()
```

`sched_unlock()`은 `lockcount`를 감소시키고, 0이 되면 `g_cpu_lockset` bit를 clear한다. 이후 다음 조건이 모두 맞으면 `up_release_pending()`으로 pending task를 ready/running으로 풀 수 있다.

- `sched_islocked_global()`이 false
- `irq_cpu_locked(cpu)`가 false
- `g_pendingtasks`가 비어 있지 않음

`sched_lock()`은 긴 user-level mutex가 아니다. interrupt handler는 계속 실행될 수 있고, SMP에서는 다른 CPU도 계속 돈다. 따라서 공유 자료구조가 interrupt나 다른 CPU와도 경쟁한다면 `sched_lock()`만으로는 부족하고 `enter_critical_section()` 또는 spinlock 계층이 필요하다.

## `sem_t`

`sem_t`는 TizenRT에서 가장 기본적인 sleep 가능한 동기화 primitive다. 구조체는 `os/include/semaphore.h`의 `struct sem_s`다.

중요 필드는 다음과 같다.

| 필드 | 의미 |
|---|---|
| `semcount` | 양수면 사용 가능한 count, 0이면 사용 가능 count 없음, 음수면 대기 task 수 |
| `flags` | initialized, signal semaphore, mutex backing semaphore, priority inheritance disable 등 |
| `holder` 또는 `hhead` | priority inheritance/recovery용 holder 정보 |

`semcount` 해석은 다음처럼 보면 된다.

| 값 | 의미 |
|---:|---|
| `> 0` | 즉시 획득 가능한 count 수 |
| `0` | 사용 가능한 count가 없고 아직 대기자는 없거나, mutex가 lock되어 있고 대기자는 없음 |
| `< 0` | `-semcount`개 task가 이 semaphore를 기다림 |

### `sem_wait()`

`sem_wait()`는 task context 전용이다. 코드에서 `up_interrupt_context() == false`를 assert한다.

흐름은 다음과 같다.

```text
sem_wait(sem)
  -> enter_critical_section()
  -> sem 유효성/FLAGS_INITIALIZED 검사
  -> semcount > 0
       semcount--
       sem_addholder()
       return OK
  -> semcount <= 0
       semcount--
       current TCB의 waitsem = sem
       priority inheritance면 sched_lock(), sem_boostpriority()
       up_block_task(rtcb, TSTATE_WAIT_SEM)
       깨어난 뒤 EINTR/ETIMEDOUT 아니면 OK
  -> leave_critical_section()
```

대기할 때 `semcount`를 먼저 감소시켜 음수로 만든다. 이후 `up_block_task()`가 현재 TCB를 `g_waitingforsemaphore`로 옮긴다. 이 list는 priority 정렬 list이므로, 같은 semaphore를 기다리는 task 중 높은 priority task가 먼저 깨어난다.

### `sem_trywait()`

`sem_trywait()`는 같은 critical section 보호를 쓰지만 block하지 않는다.

- `semcount > 0`이면 감소시키고 holder를 추가한다.
- `semcount <= 0`이면 `EAGAIN`을 설정하고 즉시 실패한다.
- interrupt context에서는 호출하면 안 된다.

### `sem_post()`

`sem_post()`는 interrupt handler에서도 호출 가능하도록 설계되어 있다.

흐름은 다음과 같다.

```text
sem_post(sem)
  -> enter_critical_section()
  -> sem_releaseholder(sem, this_task())
  -> semcount++
  -> sem_unblock_task(sem, this_task())
       semcount <= 0이면 g_waitingforsemaphore에서 같은 sem을 기다리는 첫 TCB 검색
       sem_addholder_tcb(stcb, sem)
       stcb->waitsem = NULL
       up_unblock_task(stcb)
       priority inheritance 복구
  -> leave_critical_section()
```

`semcount`를 증가시킨 뒤에도 `semcount <= 0`이면 아직 wait queue에 task가 있다는 뜻이다. 예를 들어 `semcount == -1`에서 post하면 0이 되고 대기 task 하나가 count를 넘겨받는다.

### timeout과 signal/cancel 경로

`sem_timedwait()`와 `sem_tickwait()`는 watchdog을 이용한다.

```text
sem_timedwait()
  -> watchdog 생성
  -> sem_trywait()
  -> 실패하면 timeout tick 계산
  -> enter_critical_section()
  -> wd_start(..., sem_timeout, pid)
  -> sem_wait()
  -> wd_cancel()
  -> leave_critical_section()
```

timeout이 발생하면 `sem_timeout()`이 `sem_waitirq(wtcb, ETIMEDOUT)`를 호출한다. signal이나 timeout으로 wait가 취소될 때 `sem_waitirq()`는 다음 작업을 한다.

- wait 중인 task가 여전히 `TSTATE_WAIT_SEM`인지 확인한다.
- priority inheritance holder priority를 복구한다.
- wait 때문에 감소시켰던 `semcount`를 다시 증가시킨다.
- `wtcb->waitsem = NULL`로 지운다.
- 해당 task의 `pterrno`에 `EINTR` 또는 `ETIMEDOUT`을 넣는다.
- `up_unblock_task(wtcb)`로 깨운다.

### priority inheritance와 holder

`CONFIG_PRIORITY_INHERITANCE=y`이면 semaphore를 획득한 task는 holder로 추적된다.

- `sem_addholder()`/`sem_addholder_tcb()`는 count를 얻은 task를 holder로 기록한다.
- `sem_boostpriority()`는 더 높은 priority task가 wait할 때 holder priority를 올린다.
- `sem_restorebaseprio()`는 post/cancel 이후 holder priority를 다시 계산한다.
- `sem_releaseholder()`는 post하는 task의 holder count를 감소시킨다.
- `sem_release_all()`은 task 종료 시 남은 holder를 정리한다.

일반 mutex처럼 쓰는 semaphore에는 priority inheritance가 유용하다. 하지만 event signaling용 semaphore는 다르다. 예를 들어 `sem_init(sem, 0, 0)` 후 task A가 wait하고 task B가 post하는 구조에서는 A가 깨어나며 holder처럼 기록될 수 있지만 A가 반드시 `sem_post()`로 release하는 모델은 아니다. 이 경우 `sem_setprotocol(sem, SEM_PRIO_NONE)`로 priority inheritance를 끄는 것이 안전하다. Realtek wrapper의 `rtw_mutex_init()`과 `rtw_spinlock_init()`도 `sem_setprotocol(..., SEM_PRIO_NONE)`을 호출한다.

### destroy/reset/recover

`sem_destroy()`는 initialized flag를 내리고 holder를 정리한다. 대기 task가 있는 semaphore를 destroy하는 동작은 POSIX적으로 undefined이며, 구현도 `semcount < 0`이면 count를 그대로 둔다.

`sem_reset()`은 semaphore count를 특정 값으로 맞추는 error handling용 API다. 내부에서 `sched_lock()`과 `enter_critical_section()`을 함께 사용하며, 대기 task가 있고 새 count가 남아 있으면 `sem_post()`를 반복해서 깨운다.

`sem_recover()`는 task가 삭제되거나 cancel될 때 호출된다. task가 semaphore를 기다리던 중이면 wait 때문에 감소한 `semcount`를 복구하고, task가 들고 있던 holder들을 release한다.

## `pthread_mutex_t`

`pthread_mutex_t`는 `sem_t` 위에 ownership과 mutex type 정책을 얹은 구조다. 구조체는 `os/include/sys/types.h`의 `struct pthread_mutex_s`다.

| 필드 | 의미 |
|---|---|
| `sem` | 실제 lock/unlock을 수행하는 backing semaphore |
| `pid` | mutex owner PID. `-1`이면 available |
| `type` | `PTHREAD_MUTEX_NORMAL`, `ERRORCHECK`, `RECURSIVE` |
| `nlocks` | recursive mutex의 lock count |
| `flink`, `flags` | robust mutex/held-list용. rtl8730e defconfig에서는 `CONFIG_PTHREAD_MUTEX_UNSAFE=y`라 제외 |

### 초기화

`pthread_mutex_init()`은 다음 순서로 동작한다.

```text
pthread_mutex_init()
  -> attr에서 pshared/proto/type 확인
  -> mutex->pid = -1
  -> sem_init(&mutex->sem, pshared, 1)
  -> mutex->sem.flags |= FLAGS_SEM_MUTEX
  -> sem_setprotocol(&mutex->sem, proto)
  -> type/nlocks 초기화
```

기본 protocol은 `PTHREAD_PRIO_INHERIT`다. 따라서 pthread mutex는 기본적으로 priority inheritance semaphore를 backing으로 쓴다.

### lock

`pthread_mutex_lock()`은 먼저 `sched_lock()`으로 mutex 내부 상태 검사와 semaphore take를 하나의 scheduler atomic 구간으로 묶는다.

흐름은 다음과 같다.

```text
pthread_mutex_lock(mutex)
  -> sched_lock()
  -> 현재 task가 이미 owner인지 검사
       RECURSIVE면 nlocks++
       ERRORCHECK면 EDEADLK
       NORMAL이면 backing sem_wait 경로로 들어가 deadlock 가능
  -> pthread_mutex_take()
       CONFIG_PTHREAD_MUTEX_UNSAFE=y이면 pthread_sem_take(&mutex->sem)
       pthread_sem_take()는 sem_wait()를 EINTR이 아닌 오류가 날 때까지 반복
  -> 성공하면 mutex->pid = getpid(), nlocks = 1
  -> sched_unlock()
```

`pthread_sem_take()`는 `sem_wait()`가 `EINTR`로 깨어나도 다시 기다린다. POSIX mutex lock이 signal 때문에 `EINTR`을 반환하지 않는 동작을 맞추기 위한 wrapper다.

### trylock

`pthread_mutex_trylock()`은 backing semaphore에 `sem_trywait()`를 사용한다.

- 즉시 획득하면 `pid`와 `nlocks`를 설정하고 `OK`를 반환한다.
- 이미 다른 task가 들고 있으면 `EBUSY`를 반환한다.
- recursive mutex를 같은 owner가 다시 잡으면 `nlocks++` 후 성공한다.
- recursive count가 `INT16_MAX`를 넘으면 `EOVERFLOW`를 반환한다.

### unlock

`pthread_mutex_unlock()`도 `sched_lock()` 안에서 owner/type을 확인한다.

```text
pthread_mutex_unlock(mutex)
  -> sched_lock()
  -> backing semcount로 locked 여부 확인
  -> ERRORCHECK/RECURSIVE 등 owner check가 필요한 type이면 pid 검사
  -> RECURSIVE이고 nlocks > 1이면 nlocks-- 후 return
  -> outermost unlock이면 pid = -1, nlocks = 0
  -> pthread_mutex_give() -> pthread_sem_give() -> sem_post()
  -> sched_unlock()
```

`PTHREAD_MUTEX_NORMAL`에서 owner가 아닌 task가 unlock하는 것은 POSIX상 undefined다. 구현 주석은 GLIBC처럼 owner가 아니어도 release를 허용하는 경우가 있음을 설명한다. 코드 수정 시 이 동작을 명시적 error check로 바꾸면 기존 wrapper/driver가 의존하던 undefined 동작이 깨질 수 있으므로 호출부를 먼저 확인해야 한다.

### destroy

`pthread_mutex_destroy()`는 mutex가 free 상태면 backing semaphore를 destroy한다. owner가 있으면 owner TCB가 살아 있는지 확인한다.

- owner가 살아 있으면 `EBUSY`
- owner가 이미 사라진 경우 `sem_reset(&mutex->sem, 1)` 후 destroy 시도
- reset 중 다른 task가 mutex를 잡으면 `EBUSY`

rtl8730e defconfig에서는 robust mutex가 꺼져 있으므로 `pthread_mutex_consistent()`/inconsistent held-list 흐름은 기본 mutex 동작의 중심이 아니다.

## `spinlock_t`

TizenRT kernel `spinlock_t`는 sleep하지 않는 busy-wait lock이다. public API는 `os/include/tinyara/spinlock.h`, ARM type/barrier 정의는 `os/arch/arm/include/spinlock.h`에 있다.

ARM 기준:

- `spinlock_t`는 `uint8_t`
- `SP_UNLOCKED == 0`, `SP_LOCKED == 1`
- `SP_DMB()`는 `dmb st`
- `SP_DSB()`는 `dsb sy`
- `SP_WFE()`/`SP_SEV()`는 config가 있으면 wait/event hint로 사용된다.

`CONFIG_ARCH_HAVE_TESTSET=y`이면 atomic test-and-set은 architecture가 제공한다. rtl8730e의 ARMv7-A path에서는 `os/arch/arm/src/armv7-a/arm_testset.S`의 `up_testset()`이 `LDREXB`/`STREXB`를 사용해 byte spinlock을 원자적으로 설정한다.

### `spin_lock()` / `spin_unlock()`

`os/kernel/semaphore/spinlock.c`의 기본 흐름은 다음과 같다.

```text
spin_lock(lock)
  -> while (up_testset(lock) == SP_LOCKED)
       SP_DSB()
       SP_WFE()
  -> SP_DMB()

spin_unlock(lock)
  -> SP_DMB()
  -> *lock = SP_UNLOCKED
  -> SP_DSB()
  -> SP_SEV()
```

주의점:

- `spin_lock()`은 non-reentrant다. 같은 CPU/task가 같은 lock을 다시 잡으면 deadlock이 날 수 있다.
- 함수 주석은 interrupt level에서 실행하지 않는 것을 전제로 한다.
- spinlock을 들고 있는 동안 `sem_wait()`, `pthread_mutex_lock()`, allocation 중 sleep 가능한 경로를 호출하면 안 된다.
- 긴 작업, I/O wait, user callback 호출을 spinlock 안에 두면 안 된다.

### `spin_lock_irqsave()`

`os/kernel/irq/irq_spinlock.c`는 spinlock과 local IRQ disable을 결합한 API를 제공한다.

```text
flags = spin_lock_irqsave(lock);
...
spin_unlock_irqrestore(lock, flags);
```

SMP에서 동작은 lock 인자에 따라 다르다.

| 호출 | 동작 |
|---|---|
| `spin_lock_irqsave(NULL)` | local IRQ를 끄고, CPU별 `g_irq_spin_count[cpu]`가 0일 때만 전역 `g_irq_spin`을 잡는다. 같은 CPU nested call 가능 |
| `spin_lock_irqsave(&lock)` | local IRQ를 끄고 caller가 넘긴 spinlock을 잡는다. 같은 lock으로 nested call하면 deadlock 가능 |
| non-SMP | `irqsave()`/`irqrestore()`와 동일하게 매크로 축약 |

이 API 주석도 `nxsem_wait`처럼 caller thread를 suspend하는 kernel API와 함께 쓰지 말라고 명시한다.

### `spin_setbit()` / `spin_clrbit()`

`spin_setbit()`과 `spin_clrbit()`은 SMP CPU bitset을 atomic하게 갱신하기 위한 helper다. 내부에서 raw `irqsave()`로 local interrupt 재진입을 막고, `setlock` spinlock을 잡은 뒤 bitset과 `orlock` 상태를 같이 갱신한다.

이 helper는 다음 전역 상태에 사용된다.

- `g_cpu_irqset` / `g_cpu_irqlock`
- `g_cpu_lockset` / `g_cpu_schedlock`

즉 scheduler lock과 critical section의 SMP 전역 상태는 결국 spinlock primitive 위에서 조립된다.

## rtl8730e Realtek wrapper 주의점

`os/board/rtl8730e/src/component/os_dep`의 wrapper는 이름이 kernel primitive와 같아 보이지만 실제 구현은 다를 수 있다.

| wrapper | 실제 구현 |
|---|---|
| `rtw_init_sema()` | `kmm_zalloc(sizeof(sem_t))` 후 `sem_init()` |
| `rtw_up_sema()` / `rtw_up_sema_from_isr()` | `sem_post()` |
| `rtw_down_sema()` | `sem_wait()` 반복 |
| `rtw_mutex_init()` | `sem_t` count 1, `sem_setprotocol(SEM_PRIO_NONE)` |
| `rtw_mutex_get()` / `rtw_mutex_put()` | `sem_wait()` / `sem_post()` |
| `rtw_spinlock_init()` | `sem_t` count 1, `sem_setprotocol(SEM_PRIO_NONE)` |
| `rtw_spin_lock()` / `rtw_spin_unlock()` | `sem_wait()` / `sem_post()` |
| `save_and_cli()` / `restore_flags()` | `enter_critical_section()` / `leave_critical_section()` |

따라서 `rtw_spin_lock()`은 TizenRT kernel `spin_lock()`과 다르다. `rtw_spin_lock()`은 sleep 가능한 semaphore lock이므로 interrupt context에서 wait 용도로 쓰면 안 된다. 반대로 kernel `spin_lock()`은 busy-wait이고 sleep할 수 없다.

## 대표 흐름

### semaphore wait/post로 task 깨우기

```text
Task A
  sem_wait(&sem)
    semcount <= 0
    semcount--
    A.waitsem = &sem
    up_block_task(A, TSTATE_WAIT_SEM)
    A는 g_waitingforsemaphore로 이동

Task B or ISR
  sem_post(&sem)
    semcount++
    g_waitingforsemaphore에서 waitsem == &sem인 첫 TCB 검색
    A.waitsem = NULL
    up_unblock_task(A)
    A는 ready/running 후보가 됨
```

### mutex lock/unlock

```text
pthread_mutex_lock(&m)
  sched_lock()
  pthread_sem_take(&m.sem)
    sem_wait(&m.sem)
  m.pid = getpid()
  m.nlocks = 1
  sched_unlock()

pthread_mutex_unlock(&m)
  sched_lock()
  m.pid = -1
  m.nlocks = 0
  pthread_sem_give(&m.sem)
    sem_post(&m.sem)
  sched_unlock()
```

### critical section과 pending task release

```text
enter_critical_section()
  local IRQ disable
  SMP global IRQ lock acquired
  shared scheduler/semaphore state update

leave_critical_section()
  last nesting exit
  if this was last IRQ lock holder and scheduler is not locked
     pending tasks may be released
  restore previous IRQ state
```

### scheduler lock과 pending task

```text
sched_lock()
  current TCB lockcount++
  SMP global scheduler lock bit set

higher priority task becomes ready
  immediate preemption is deferred
  task goes to g_pendingtasks

sched_unlock()
  current TCB lockcount--
  if lockcount == 0 and no global sched/IRQ lock
     up_release_pending()
```

## 수정할 때 지켜야 할 규칙

### block 가능 여부를 먼저 판단한다

다음 함수들은 block 가능성이 있다.

- `sem_wait()`
- `sem_timedwait()`
- `pthread_mutex_lock()`
- `pthread_mutex_destroy()` 일부 경로의 `sem_reset()`
- allocation/free 내부에서 semaphore를 잡는 memory manager 경로

이 함수들은 interrupt context, `spin_lock()` 보유 중, `spin_lock_irqsave()` 보유 중, 길게 열린 critical section 안에서 호출하지 않는다.

### `sched_lock()`을 mutex처럼 쓰지 않는다

`sched_lock()`은 현재 task의 선점을 막는 도구다. interrupt와 다른 CPU 접근까지 막아야 하는 자료구조라면 `sched_lock()`만으로 부족하다.

반대로 interrupt와 경쟁하지 않고 현재 task가 context switch만 당하지 않으면 되는 짧은 내부 상태 검사는 `sched_lock()`이 더 가볍고 의도도 명확하다. pthread mutex 구현이 owner/type 검사 주변에서 `sched_lock()`을 쓰는 이유가 여기에 가깝다.

### event semaphore는 priority inheritance를 끄는 것을 검토한다

count 0으로 시작해서 "누군가 post하면 기다리던 task가 깨어나는" event semaphore는 mutex와 release 모델이 다르다. 이 경우 holder tracking이 오히려 priority inheritance 계산을 오염시킬 수 있다.

권장 패턴:

```c
sem_init(&event_sem, 0, 0);
sem_setprotocol(&event_sem, SEM_PRIO_NONE);
```

단, binary manager recovery 때문에 holder tracking이 필요한 구조에서는 기존 정책을 먼저 확인해야 한다.

### lock 이름만 보고 판단하지 않는다

특히 wrapper 계층에서 `spinlock`, `mutex`, `critical`이라는 이름이 kernel primitive와 1:1로 대응하지 않는다.

- `rtw_spin_lock()`은 `sem_wait()` 기반이다.
- `save_and_cli()`는 `enter_critical_section()`이다.
- kernel `spin_lock()`은 `spinlock_t` busy-wait이다.
- `spin_lock_irqsave(NULL)`은 nesting 가능한 전역 lock이고, `spin_lock_irqsave(&lock)`은 caller lock이다.

### lock 순서를 단순하게 유지한다

일반적인 안전 순서는 다음과 같다.

1. block 가능한 lock이 필요하면 먼저 `pthread_mutex_lock()` 또는 `sem_wait()`를 호출한다.
2. 그 안에서 아주 짧게 interrupt/SMP와 공유되는 필드만 `enter_critical_section()` 또는 `spin_lock_irqsave()`로 보호한다.
3. critical/spin section을 먼저 풀고, 그 다음 sleep lock을 푼다.

반대 순서, 즉 critical/spin을 잡은 상태에서 sleep lock을 기다리는 구조는 deadlock과 latency 문제를 만든다.

## 빠른 체크리스트

- 이 코드가 interrupt context에서 호출될 수 있는가?
- 이 lock을 잡은 상태에서 block 가능한 함수가 호출되는가?
- SMP에서 다른 CPU가 같은 자료구조를 만질 수 있는가?
- 단순 preemption 방지만 필요한가, IRQ disable까지 필요한가?
- `sem_t`가 mutex인지 event인지 명확한가?
- priority inheritance가 필요한 semaphore인가?
- `FLAGS_SEM_MUTEX`, `FLAGS_SIGSEM`, `PRIOINHERIT_FLAGS_DISABLE` 의미를 깨지 않는가?
- `enter_critical_section()`의 flags를 반드시 같은 scope에서 `leave_critical_section(flags)`로 복구하는가?
- `spin_lock_irqsave(NULL)`과 `spin_lock_irqsave(&lock)`의 nesting 차이를 이해하고 쓰는가?
- wrapper의 lock 이름이 실제 kernel primitive와 같은지 확인했는가?
