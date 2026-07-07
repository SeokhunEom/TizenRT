# Semaphore Operation Guide

이 문서는 TizenRT semaphore를 기초 개념부터 내부 구현까지 정리한다. 특히 `semcount`, `semholder_s`, `tcb_s::holdsem`, `tcb_s::waitsem`의 관계와 priority inheritance가 왜 holder 정보를 필요로 하는지 설명한다.

## 핵심 요약

- `sem_t`의 `semcount`는 semaphore 자체의 count 상태다. 양수는 즉시 획득 가능한 count 수이고, 0 이하에서는 사용 가능한 count가 없거나 기다리는 task가 있는 상태다.
- `tcb_s::waitsem`은 task가 현재 `sem_wait()`에서 기다리는 semaphore다. 이미 획득한 semaphore 목록이 아니다.
- `tcb_s::holdsem`은 priority inheritance가 켜졌을 때 task가 현재 들고 있는 semaphore holder node 목록이다.
- `semholder_s`는 `sem_t`와 `tcb_s`를 잇는 중간 노드다. 같은 node가 semaphore 기준 holder list와 task 기준 held-semaphore list에 동시에 연결된다.
- `holder->counts`는 한 task가 특정 semaphore에서 들고 있다고 기록된 count 개수다. holder가 1명이라는 말은 holder node가 하나라는 뜻이지 `counts == 1`이라는 뜻이 아니다.
- holder tracking은 semaphore의 기본 wait/post 기능이 아니라 priority inheritance와 task 종료 복구를 위한 bookkeeping이다.
- lock처럼 같은 task가 `sem_wait()`와 `sem_post()`를 짝지어 호출하면 holder 정보가 자연스럽다.
- signaling처럼 A가 wait하고 B가 post하는 모델에서는 holder ownership이 불명확하다. 이 용도에는 `sem_setprotocol(SEM_PRIO_NONE)`으로 priority inheritance를 끄는 것이 원칙이다.

## 주요 파일

| 관심사 | 파일 |
|---|---|
| public semaphore 타입과 flag | `os/include/semaphore.h` |
| TCB의 semaphore 관련 필드 | `os/include/tinyara/sched.h` |
| semaphore 초기화 | `lib/libc/semaphore/sem_init.c` |
| wait/trywait/post 경로 | `os/kernel/semaphore/sem_wait.c`, `sem_trywait.c`, `sem_post.c` |
| wait 취소와 timeout 복구 | `os/kernel/semaphore/sem_waitirq.c`, `sem_timedwait.c`, `sem_tickwait.c` |
| holder 관리와 priority inheritance | `os/kernel/semaphore/sem_holder.c` |
| semaphore protocol 설정 | `os/kernel/semaphore/sem_setprotocol.c` |
| 내부 helper 선언 | `os/kernel/semaphore/semaphore.h` |

## 기본 개념

Semaphore는 count 기반 동기화 primitive다. TizenRT의 public 타입은 `struct sem_s`이고 `sem_t`로 typedef된다.

```c
struct sem_s {
	int16_t semcount;
	uint8_t flags;
#ifdef SAVE_SEM_HOLDER
#if CONFIG_SEM_PREALLOCHOLDERS > 0
	FAR struct semholder_s *hhead;
#else
	struct semholder_s holder;
#endif
#endif
};
```

기본 동작은 단순하다.

```text
sem_wait() 성공
  -> semcount 감소

sem_post()
  -> semcount 증가
```

`semcount > 0`이면 호출 task가 block 없이 count를 가져갈 수 있다. `semcount <= 0`이면 가용 count가 없으므로 `sem_wait()`는 현재 task를 `TSTATE_WAIT_SEM` 상태로 block시킬 수 있다.

TizenRT의 `SEM_VALUE_MAX`는 `_POSIX_SEM_VALUE_MAX`인 `0x7fff`다. `sem_init()`은 초기 value가 이 범위 안인지 검사하고 `sem->semcount`에 저장한다.

## `sem_t`, `tcb_s`, `semholder_s` 관계

Semaphore 동작만 보면 `semcount`와 wait queue만 있어도 된다. 하지만 priority inheritance를 하려면 kernel이 "이 semaphore count를 누가 들고 있는가"를 알아야 한다. 그래서 holder node가 있다.

```c
struct semholder_s {
#if CONFIG_SEM_PREALLOCHOLDERS > 0
	struct semholder_s *flink;
#endif
#ifdef CONFIG_PRIORITY_INHERITANCE
	FAR struct semholder_s *tlink;
	FAR struct sem_s *sem;
#endif
	FAR struct tcb_s *htcb;
	int16_t counts;
};
```

각 필드의 의미는 다음과 같다.

| 필드 | 의미 |
|---|---|
| `semholder_s::sem` | 이 holder node가 어느 semaphore에 속하는지 |
| `semholder_s::htcb` | 이 semaphore count를 들고 있는 holder task |
| `semholder_s::counts` | `htcb`가 이 semaphore에서 들고 있는 count 개수 |
| `semholder_s::flink` | 같은 `sem_t`의 holder list에서 다음 holder |
| `semholder_s::tlink` | 같은 `tcb_s`의 held-semaphore list에서 다음 holder |
| `sem_t::hhead` 또는 `sem_t::holder` | semaphore 기준 holder 목록 |
| `tcb_s::holdsem` | task 기준 holder 목록 |
| `tcb_s::waitsem` | task가 지금 기다리는 semaphore |

같은 holder node가 두 list에 동시에 연결된다.

```text
sem1.hhead
  |
  v
node X -----------------> node Y
  |                         |
  | htcb = Task A           | htcb = Task B
  | sem  = sem1             | sem  = sem1
  | counts = 2              | counts = 1
  |
  +-- Task A.holdsem 에서도 같은 node X로 보임
```

예를 들어 Task A가 `sem1` count 2개와 `sem2` count 1개를 들고 있고, Task B가 `sem1` count 1개를 들고 있으면 구조는 이렇게 해석된다.

```text
sem1 holder list:
- node X: sem=sem1, htcb=Task A, counts=2
- node Y: sem=sem1, htcb=Task B, counts=1

sem2 holder list:
- node Z: sem=sem2, htcb=Task A, counts=1

Task A holdsem list:
- node Z: sem=sem2, htcb=Task A, counts=1
- node X: sem=sem1, htcb=Task A, counts=2

Task B holdsem list:
- node Y: sem=sem1, htcb=Task B, counts=1
```

`sem_t` 쪽 list는 "이 semaphore를 누가 들고 있는가"를 찾기 위해 필요하다. 높은 priority task가 이 semaphore에서 block되면 holder task들의 priority를 boost해야 한다.

`tcb_s::holdsem` 쪽 list는 "이 task가 어떤 semaphore들을 들고 있는가"를 찾기 위해 필요하다. task가 종료되거나 recovery될 때 그 task가 들고 있던 holder들을 한 번에 해제한다.

`tcb_s::waitsem`은 다른 정보다. 이것은 이미 들고 있는 semaphore가 아니라, 현재 `sem_wait()`에서 block되어 기다리는 semaphore를 가리킨다.

```text
holdsem
  이미 획득한 semaphore holder 목록

waitsem
  지금 block되어 기다리는 semaphore 하나
```

## Holder node 생성과 제거

`sem_allocholder()`는 새 holder node를 준비한다.

```text
sem_allocholder(sem, htcb)
  -> CONFIG_SEM_PREALLOCHOLDERS > 0이면 free holder pool에서 하나 꺼냄
  -> pholder->flink = sem->hhead
  -> sem->hhead = pholder
  -> pholder->sem = sem
  -> pholder->htcb = htcb
  -> pholder->counts = 0
  -> pholder->tlink = htcb->holdsem
  -> htcb->holdsem = pholder
```

즉 node를 만들 때 semaphore 기준 list와 task 기준 list에 모두 연결한다.

`sem_freeholder()`는 반대로 두 list에서 모두 제거한다.

```text
sem_freeholder(sem, pholder)
  -> pholder를 pholder->htcb->holdsem list에서 제거
  -> pholder->tlink = NULL
  -> pholder->sem = NULL
  -> pholder->htcb = NULL
  -> pholder->counts = 0
  -> CONFIG_SEM_PREALLOCHOLDERS > 0이면 sem->hhead list에서 제거
  -> holder node를 free list로 반환
```

이 양방향 정리가 맞지 않으면 한쪽에서는 holder가 보이고 다른 쪽에서는 보이지 않는 dangling bookkeeping 상태가 된다.

## `sem_init()`

`sem_init()`은 다음 상태를 만든다.

```text
sem_init(sem, pshared, value)
  -> value <= SEM_VALUE_MAX 검사
  -> sem->semcount = value
  -> sem->flags = FLAGS_INITIALIZED
  -> priority inheritance가 켜졌으면 PRIOINHERIT_FLAGS_DISABLE 해제
  -> holder 저장 영역 초기화
  -> value == 0이면 FLAGS_SIGSEM 설정
```

`value == 0`인 semaphore는 보통 event signaling에 사용된다. TizenRT는 이 경우 `FLAGS_SIGSEM`을 세워 holder 저장을 건너뛰는 경로를 둔다. 다만 모든 signaling 문제가 이 flag 하나로 사라지는 것은 아니다. 이미 holder tracking이 들어간 semaphore를 inter-task signaling처럼 쓰거나 protocol을 잘못 두면 holder accounting이 엇갈릴 수 있다.

## `sem_wait()` 흐름

가용 count가 있으면 `sem_wait()`는 block 없이 count를 가져간다.

```text
sem_wait(sem)
  -> enter_critical_section()
  -> sem->semcount > 0
  -> sem->semcount--
  -> sem_addholder(sem)
  -> current->waitsem = NULL
  -> leave_critical_section()
```

이때 `sem_addholder()`는 현재 task를 holder로 기록한다. 이미 같은 task의 holder node가 있으면 그 node의 `counts`만 증가하고, 없으면 새 holder node를 만든다.

가용 count가 없으면 현재 task는 wait queue로 들어간다.

```text
sem_wait(sem)
  -> sem->semcount <= 0
  -> sem->semcount--
  -> current->waitsem = sem
  -> priority inheritance가 켜져 있으면 sem_boostpriority(sem)
  -> up_block_task(current, TSTATE_WAIT_SEM)
  -> 나중에 sem_post(), signal, timeout으로 깨어남
```

여기서 `semcount`가 음수가 될 수 있다. 음수의 절댓값은 이 semaphore를 기다리는 task 수를 나타내는 방식으로 쓰인다.

## `sem_post()` 흐름

`sem_post()`는 interrupt handler에서도 호출될 수 있으므로 critical section 안에서 semaphore count와 wait list를 처리한다.

```text
sem_post(sem)
  -> enter_critical_section()
  -> sem_releaseholder(sem, this_task())
  -> sem->semcount++
  -> sem_unblock_task(sem, this_task())
  -> leave_critical_section()
```

`sem_releaseholder()`는 post를 호출한 task의 holder node를 찾고, 있으면 `counts`를 하나 줄인다. 이 감소는 "이 task가 semaphore count 하나를 release했다"는 holder bookkeeping이다.

그 뒤 `sem_unblock_task()`는 `sem->semcount <= 0`이면 `g_waitingforsemaphore`에서 이 semaphore를 기다리는 task를 찾아 깨운다.

```text
sem_unblock_task(sem, htcb)
  -> sem->semcount <= 0이면 waiting task 검색
  -> stcb->waitsem == sem인 task를 찾음
  -> sem_addholder_tcb(stcb, sem)
  -> stcb->waitsem = NULL
  -> up_unblock_task(stcb)
  -> sem_restorebaseprio(stcb, htcb, sem)
```

깨어난 task는 이제 count를 받은 holder가 되므로 `sem_addholder_tcb(stcb, sem)`로 holder list에 추가된다. post한 task는 count를 release했으므로 priority를 원래대로 돌릴 수 있는지 `sem_restorebaseprio()`에서 확인한다.

## `sem_waitirq()`와 wait 취소

`sem_wait()`가 block된 뒤 signal이나 timeout으로 깨면, 이미 `semcount`를 감소시킨 상태를 되돌려야 한다. 이 정리는 `sem_waitirq()`에서 수행된다.

```text
sem_waitirq(wtcb, errcode)
  -> wtcb->task_state == TSTATE_WAIT_SEM 확인
  -> sem = wtcb->waitsem
  -> sem_canceled(wtcb, sem)
  -> sem->semcount++
  -> wtcb->waitsem = NULL
  -> wtcb->pterrno = errcode
  -> up_unblock_task(wtcb)
```

중요한 점은 취소가 `sem_wait()`로 돌아온 뒤에 처리되는 것이 아니라, 깨우는 쪽에서 먼저 semaphore count와 priority 상태를 복구한다는 것이다. 그래야 signal 발생과 실제 task 재개 사이의 race window를 줄일 수 있다.

## Priority inheritance가 holder를 쓰는 이유

Priority inversion 예시는 다음과 같다.

```text
Task L: 낮은 priority
Task H: 높은 priority

초기 semcount = 1

Task L: sem_wait()
  -> semcount = 0
  -> holder list에 L 기록

Task H: sem_wait()
  -> semcount < 1이라 block
  -> sem_boostpriority(sem)
  -> holder L을 찾아 L priority를 H 수준으로 boost

Task L: sem_post()
  -> L holder count 감소
  -> H unblock
  -> L priority 복원
```

이때 kernel이 holder를 모르면 "누구 priority를 올려야 하는지" 알 수 없다. 그래서 `sem_t` 기준 holder list가 필요하다.

복원도 holder 정보가 필요하다. `sem_restorebaseprio()`는 post 이후 holder들의 priority를 다시 계산한다. holder가 더 이상 count를 들고 있지 않으면 `sem_freeholder()`로 holder node를 제거한다.

## Locking semaphore와 signaling semaphore

Semaphore는 두 사용 모델이 있다.

### Locking model

같은 task가 wait와 post를 짝지어 호출한다.

```text
Task A: sem_wait(sem)
Task A: critical section
Task A: sem_post(sem)
```

이 모델에서는 holder가 자연스럽다. Task A가 count를 들고 있고, Task A가 release한다.

### Signaling model

한 task가 기다리고 다른 task나 interrupt가 post한다.

```text
Task A: sem_wait(sem)
Task B: sem_post(sem)
```

이 모델에서는 holder ownership이 자연스럽지 않다. Task A는 wakeup 후 holder로 기록될 수 있지만, 실제 post는 Task B가 수행한다. Task B가 holder가 아니면 `sem_releaseholder(sem, Task B)`는 어느 holder count를 줄여야 하는지 알기 어렵다.

그래서 signaling semaphore에는 priority inheritance를 끄는 것이 원칙이다.

```c
sem_t sem;

sem_init(&sem, 0, 0);
sem_setprotocol(&sem, SEM_PRIO_NONE);
```

`sem_setprotocol(SEM_PRIO_NONE)`은 `PRIOINHERIT_FLAGS_DISABLE`을 세운다. `CONFIG_BINMGR_RECOVERY`가 꺼진 구성에서는 현재 holder들도 정리한다.

## Holder가 1명인 경우와 여러 명인 경우

"holder가 1명"이라는 말은 holder node가 하나라는 뜻이다. 그 holder의 `counts`는 1보다 클 수 있다.

```text
holder list:
- Task A, counts = 3

=> holder는 1명
=> Task A가 이 semaphore count 3개를 들고 있다고 기록됨
```

`c93078ab05bb6463467669fb6ee19bb75ee7eaba` 이전의 `sem_releaseholder()`는 post한 task의 holder node만 찾았다.

```text
holder list:
- Task A, counts = 1

Task B: sem_post()

기존 구현:
  -> Task B holder를 찾음
  -> 없음
  -> 아무 holder count도 줄이지 못함
```

`os/kernel/semaphore: Fix holder count overflow in sem_releaseholder` 변경은 이 단일 holder case를 보완한다.

```text
holder list:
- Task A, counts = 1

Task B: sem_post()

보완 후:
  -> Task B holder 없음
  -> recorded holder가 Task A 하나뿐임
  -> release 대상이 유일하므로 Task A.counts--
  -> sem_restorebaseprio()가 Task A holder를 정리할 수 있도록 Task A TCB 반환
```

하지만 holder가 여러 명이면 자동으로 맞힐 수 없다.

```text
holder list:
- Task A, counts = 1
- Task C, counts = 1

Task B: sem_post()
```

이 호출만 보고는 B의 post가 A의 count를 release한 것인지 C의 count를 release한 것인지 알 수 없다. 잘못된 holder를 감소시키면 priority inheritance 상태가 더 틀어질 수 있다. 따라서 안전한 해법은 이런 semaphore를 signaling 용도로 보고 priority inheritance holder tracking을 끄는 것이다.

## NuttX와 비교할 때의 핵심 차이

NuttX에도 같은 문제의식이 있었다. 과거 NuttX 변경은 non-holder가 post했을 때 holder가 하나뿐이면 그 holder count를 감소시키는 보완을 넣었다.

NuttX mainline의 방향은 "여러 holder가 있는 counting semaphore에서 non-holder post가 발생하면 올바른 holder를 알 수 없다"는 점을 명시하고, signaling semaphore에서는 priority inheritance를 끄라고 안내하는 쪽이다. 즉 여러 holder 중 하나를 추측해 release하는 방식으로 해결하지 않는다.

TizenRT에서도 설계 판단은 같다.

- holder가 하나뿐이면 release 대상이 유일하므로 제한적으로 보정할 수 있다.
- holder가 여러 명이면 `sem_post()` API만으로 대상 holder를 알 수 없다.
- event signaling 용도라면 `SEM_PRIO_NONE`을 적용하는 것이 맞다.
- resource ownership이 필요한 용도라면 semaphore보다 mutex나 ownership이 명확한 별도 primitive를 검토한다.

## Task 종료와 `sem_release_all()`

`tcb_s::holdsem`은 task 종료 복구에서 중요하다.

```text
sem_release_all(htcb)
  -> while ((pholder = htcb->holdsem) != NULL)
  -> sem = pholder->sem
  -> sem_freeholder(sem, pholder)
  -> sem->semcount++
```

이 경로는 task가 holder count를 들고 종료될 때 holder node를 회수하고 semaphore count를 복구한다. 그래서 `semholder_s::sem`과 `tcb_s::holdsem` 연결이 필요하다. semaphore 쪽 list만 있으면 "이 task가 들고 있던 모든 semaphore"를 찾으려면 전체 semaphore를 스캔해야 한다.

## 읽을 때 자주 헷갈리는 지점

| 질문 | 답 |
|---|---|
| `semcount`와 holder `counts`는 같은 값인가? | 아니다. `semcount`는 semaphore 전체 count 상태이고, holder `counts`는 특정 task가 들고 있는 count 개수다. |
| holder가 1명이라는 말은 `counts == 1`인가? | 아니다. holder node가 하나라는 뜻이다. 그 node의 `counts`는 2 이상일 수 있다. |
| `waitsem`은 held semaphore인가? | 아니다. 현재 block되어 기다리는 semaphore다. held semaphore는 `holdsem` list에 있다. |
| 왜 `sem_t`와 `tcb_s` 양쪽에 holder 연결이 있는가? | semaphore 기준으로 holder들을 찾아 priority boost하고, task 기준으로 held semaphore들을 찾아 종료 복구하기 위해서다. |
| signaling semaphore에도 priority inheritance를 써야 하는가? | 원칙적으로 쓰지 않는다. post task와 holder task가 다를 수 있어 holder accounting이 불명확해진다. |
| 여러 holder가 있고 non-holder가 post하면 올바른 holder를 고를 수 있는가? | `sem_post()` 호출만으로는 고를 수 없다. API에 release 대상 정보가 없기 때문이다. |

## 점검 체크리스트

Semaphore 관련 버그를 볼 때는 다음 순서로 확인한다.

1. 이 semaphore가 lock 용도인지 signaling 용도인지 먼저 구분한다.
2. 초기값이 0이고 다른 task나 interrupt가 post하는 구조라면 `sem_setprotocol(SEM_PRIO_NONE)` 적용 여부를 확인한다.
3. `semcount`만 보지 말고 holder list의 task 수와 각 `counts`를 함께 본다.
4. `tcb_s::waitsem`이 남아 있으면 task가 아직 wait 상태인지, signal/timeout 복구가 지나갔는지 확인한다.
5. `tcb_s::holdsem`이 남아 있으면 해당 task가 실제로 count를 들고 있는지, 종료 복구가 필요한지 확인한다.
6. post한 task가 holder인지 확인한다. holder가 아니면 단일 holder case인지, 여러 holder라서 원리적으로 모호한 case인지 구분한다.
7. holder count가 `SEM_VALUE_MAX` 근처로 커진다면 release path가 holder count를 줄이지 못하는 accounting drift를 의심한다.
