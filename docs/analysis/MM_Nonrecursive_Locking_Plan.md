# MM 재귀 획득 제거 구조 계획

작성일: 2026-09-17\
기준: `codex/docs`, `195e121b9cd35193098a12caeb4ff0cc5dde4a3c`\
상태: 현재 소스에 근거한 설계. 구현·빌드·보드 실행은 하지 않음.

## 1. 가능 여부와 목표

**정상 호출의 중첩 lock 획득은 제거할 수 있다. 전체 비재귀화도 가능하지만, signal·취소·종료의 실행 시점까지 함께 정리해야 한다.**

다음 두 목표를 구분한다.

- 1차 목표: `sched_ufree/sched_kfree → free → mm_free`의 이중 획득을 없애고, 해제 정책을 MM 안으로 모은다.
- 최종 목표: 지원하는 모든 설정에서 같은 heap의 lock을 보유한 실행 흐름이 다시 lock을 획득하지 않는다. 비동기 처리는 lock 밖으로 지연하며, 확인되지 않은 재진입은 무조건 성공시키지 않는다.

초깃값 1인 semaphore는 계속 사용할 수 있다. 제거 대상은 `mm_holder == my_pid`일 때 토큰 획득을 생략하는 동작과 재귀 깊이에 따른 반환이다. 단순히 pthread mutex로 교체하는 것으로 호출 구조와 비동기 문제까지 해결되지는 않는다.

## 2. 현재 확인한 경로

| 경로 | 현재 동작 | 변경 방향 |
| --- | --- | --- |
| `sched_ufree/sched_kfree` | `trysemaphore`로 획득한 뒤 일반 free가 재획득 | 한 번의 `tryfree`로 통합 |
| signal 처리 후 동적 signal 객체 반환 | `sig_deliver → sig_releasependingsigaction → sched_kfree` | 동일 heap이 잠겼으면 지연 큐로 이동 |
| `malloc`의 delay list 처리 | malloc 자체 lock 획득 **전** free 호출 | 현재의 lock 밖 실행 순서를 유지 |
| `realloc`의 새 블록 할당 | 기존 lock 반환 **후** malloc/free 호출 | 유지. 불필요한 전체 allocator 분할은 하지 않음 |
| `mm_extend` | lock 반환 후 free 호출 | 현재 중첩 획득은 아님. 재귀 제거 때문에 변경할 필요 없음 |
| heap 진단·로그 | lock 안에서 출력, 로그 압축 경로는 `IS_KMM_LOCKED()` 사용 | 잠금 중 출력은 무할당·비대기 경로로 제한 |
| signal handler의 같은 heap 할당 | 중단된 task와 같은 PID로 들어올 수 있음 | handler 실행을 MM 안전 지점까지 지연하는 정책 필요 |
| interrupt에서 직접 MM 호출 | `mm_takesemaphore()`의 interrupt 거절은 현재 SMP 조건 안에 있음 | UP/SMP 모두 free는 지연, 일반 할당은 진입 전 거절 |
| exit/cancel/recovery | MM 보유 중 종료 및 하위 semaphore 회수가 가능 | 보유 구간의 정상 종료는 지연, 강제 회복 정책은 별도 검증 |

근거:

- [sched_free.c:102](../../os/kernel/sched/sched_free.c#L102), [mm_free.c:121](../../os/mm/mm_heap/mm_free.c#L121)
- [sig_deliver.c:209](../../os/kernel/signal/sig_deliver.c#L209), [sig_releasependingsigaction.c:130](../../os/kernel/signal/sig_releasependingsigaction.c#L130)
- [mm_malloc.c:156](../../os/mm/mm_heap/mm_malloc.c#L156), [mm_realloc.c:367](../../os/mm/mm_heap/mm_realloc.c#L367), [mm_extend.c:136](../../os/mm/mm_heap/mm_extend.c#L136)
- [mem_leak_checker.c:345](../../os/kernel/debug/mem_leak_checker.c#L345), [mm_heapinfo_parse_heap.c:176](../../os/mm/mm_heap/mm_heapinfo_parse_heap.c#L176)
- [log_dump.c:256](../../os/kernel/log_dump/log_dump.c#L256), [log_dump.c:415](../../os/kernel/log_dump/log_dump.c#L415), [mm.h:465](../../os/include/tinyara/mm/mm.h#L465)
- [mm_sem.c:144](../../os/mm/mm_heap/mm_sem.c#L144)
- [task_exithook.c:612](../../os/kernel/task/task_exithook.c#L612), [pthread_cancel.c:144](../../os/kernel/pthread/pthread_cancel.c#L144)

표의 handler 재진입은 설정과 실제 전달 시점에 의존하는 경로다. 모든 보드에서의 발생이나 특정 장애의 원인으로 확정한 것은 아니다. 로그 호출도 전부 실제 재귀 할당이라고 단정하지 않고, 출력 backend별 확인 대상으로 본다.

## 3. 1차 변경: 해제를 한 번의 연산으로 제공

### 3.1 내부 구현 분리

`mm_free.c` 안에서 기존 `mm_free_internal()`을 다음 책임으로 나눈다. 이름은 제안이다.

```c
/* static: 호출자가 해당 heap lock을 이미 획득했어야 한다. */
static void mm_free_locked(struct mm_heap_s *heap, void *mem,
                           mmaddress_t caller, pid_t caller_pid);

/* MM 내부 interface: 대기 없이 한 번의 해제를 시도한다. */
int mm_tryfree(struct mm_heap_s *heap, void *mem,
               mmaddress_t caller, pid_t caller_pid);
```

`mm_free_locked()`는 블록 검증·병합·free list 및 통계 갱신만 담당한다. lock 획득/반환, malloc/free, 일반 로그 backend, 사용자 callback, GC를 호출하지 않는다. 오류 진단은 MM을 사용하지 않는 경로로 처리한다.

일반 `mm_free()`와 `mm_free_withinfo()`는 진입 문맥과 metadata를 처리하고, 한 번 획득한 뒤 이 내부 함수를 호출하고 반환한다. `mm_tryfree()`도 같은 코어를 사용하되 lock을 기다리지 않는다. 이 단계에서 malloc/realloc/memalign 전부에 `_locked` 변형을 만들 필요는 없다.

### 3.2 tryfree의 계약

- `0`: 해제 완료. NULL은 명시적인 no-op 성공으로 처리한다.
- `-EAGAIN`: lock을 얻지 못함. heap과 대상 블록을 변경하지 않았고, 포인터 소유권은 호출자에게 남는다.
- 주소로 heap을 찾지 못한 경우 등 다른 오류: 진단 경로로 전달한다. 이를 단순 busy로 취급해 지연 큐에 넣지 않는다.
- 같은 task가 이미 해당 lock을 가지고 있어도 try는 실패해야 한다. 기존 재귀 `mm_trysemaphore()`를 그대로 호출하면 목표를 달성하지 못한다.
- 성공한 경우만 정확히 한 번 반환한다. 오류 반환을 무시하고 heap 수정을 계속하는 경로가 없어야 한다.

UMM/KMM에는 주소에 맞는 heap을 선택하는 얇은 `umm_tryfree/kmm_tryfree` wrapper를 둔다. `kumm_*` 및 kernel heap이 없는 설정의 alias도 함께 연결한다. `mm_free_locked()`를 scheduler에 공개하지 않는다.

```text
sched_ufree / sched_kfree
  ├─ interrupt 문맥 → 기존 지연 큐로 소유권 이전
  └─ umm_tryfree / kmm_tryfree
       ├─ heap 선택
       ├─ 비재귀 try 획득 실패 → -EAGAIN → 지연 큐로 소유권 이전
       └─ 획득 성공
            ├─ mm_free_locked       // 잠금을 다시 잡지 않음
            └─ lock 반환           // 정확히 한 번
```

`sched_*free`의 바깥 try/give 쌍은 제거한다. 일반 free를 호출하기 전에 바깥 lock을 잠깐 푸는 방식은 다른 task가 끼어들어 대기하게 될 수 있으므로 사용하지 않는다.

### 3.3 지연 해제의 완료 보장

우선 기존 `g_delayed_kfree/g_delayed_kufree`와 allocator의 per-CPU delay list를 유지하고, 한 포인터가 두 큐에 동시에 들어가지 않도록 소유권을 명시한다. 큐 통합은 별도 개선 사항이다.

idle/worker의 큐 소비 경로도 함께 검토한다. 현재 [sched_garbage.c:96](../../os/kernel/sched/sched_garbage.c#L96)는 큐에서 꺼내 일반 free를 호출한다. 비대기 소비로 전환할 때에는:

- 한 번의 pass에서 기존 항목만 유한하게 처리하고, busy 항목은 다음 pass로 넘긴다.
- 방금 다시 넣은 busy 항목을 즉시 꺼내 반복하는 무한 루프를 만들지 않는다.
- worker가 없는 idle 설정에서도 최종 반환이 진행되는지 확인한다.
- unlock 경로에서 일반 GC를 직접 호출하지 않는다. unlock 자체의 재진입과 지연을 만들 수 있다.
- app heap 삭제/unload 전에 그 heap을 가리키는 지연 항목을 drain하거나 안전하게 정리한다. 해제된 heap에 나중에 접근해서는 안 된다.

## 4. 최종 비재귀화의 선행 조건

### 4.1 signal과 취소를 안전 지점으로 이동

같은 task가 lock을 가진 도중 handler가 같은 heap을 할당하면, 비재귀 lock에서는 handler가 바깥 호출을 기다리고 바깥 호출은 handler 종료를 기다린다. `_locked` 함수 분리만으로 해결할 수 없다.

권장 정책은 **MM이 heap을 수정할 수 있는 구간과 lock 상태 전환 중에는 해당 task의 handler 및 정상적인 종료·취소 처리를 지연하고, heap과 lock이 안정된 뒤 실행하는 것**이다. 애플리케이션의 signal mask 자체와는 구별되는 커널 내부 상태로 관리한다.

구현해야 할 상태 계약은 다음과 같다.

| 상태 | signal/cancel 정책 |
| --- | --- |
| 미보유·대기 중 | 소유권이 없다는 조건 아래 기존 전달/취소 동작을 유지 |
| 토큰 부여·소유 상태 게시 | 토큰 획득과 전달 지연 상태를 같은 커널 동기화 구간에서 변경 |
| 보유 중 | handler 및 정상 종료를 pending으로 보관. 같은 heap에 새로 진입하지 않음 |
| 반환 중 | heap 변경 완료 후 토큰과 소유 상태를 일관되게 반환 |
| 반환 완료 | pending 처리를 안전 지점에서 재개 |

이는 **MM 전용 lock 획득·반환 interface와 signal/scheduler의 연동 작업**이다. `sem_wait()`가 성공해 돌아온 뒤 user 코드에서 플래그만 세우면, 그 사이에 signal이 실행될 수 있으므로 불충분하다. 대기 task에게 토큰을 넘겨주는 경로도 같은 계약에 포함한다. 대기 중 handler가 MM을 이용한 뒤 복귀하는 경우와 취소되는 경우에는 중단된 wait 상태 및 토큰 소유권도 보존해야 한다.

현재 `sem_wait()`는 cancellation point이고([sem_wait.c:151](../../os/kernel/semaphore/sem_wait.c#L151)), signal mask를 복원하는 함수 안에서도 pending signal을 실행할 수 있다([sig_procmask.c:188](../../os/kernel/signal/sig_procmask.c#L188)). 따라서 MM 파일에 `sched_lock()`이나 단순 signal-mask save/restore를 추가하는 것으로 완료 처리하지 않는다. 대기 시간 전체를 무조건 signal 차단 구간으로 만드는 방법도 전달·취소 지연이라는 별도 동작 변경을 초래한다.

Protected UMM에서는 커널 TCB/IRQ 상태를 직접 변경할 수 있다고 가정하지 않는다. MM 전용 커널 진입 경로와 kernel/user 빌드 연결이 필요하다. SMP에서는 다른 CPU의 signal·취소 요청과 토큰 인계까지 같은 동기화 규칙을 따라야 한다. 정확한 syscall 형태와 아키텍처별 연결은 해당 단계의 구현 전 설계 항목이다.

signal pending 객체 자체도 메모리를 요구할 수 있다([sig_allocatependingsigaction.c:130](../../os/kernel/signal/sig_allocatependingsigaction.c#L130), [sig_dispatch.c:190](../../os/kernel/signal/sig_dispatch.c#L190)). 전달을 미뤘다는 이유만으로 안전하다고 가정하지 않고, MM 보유 문맥에서 실행 가능한 enqueue 경로는 사전 할당 저장소 등 heap에 재진입하지 않는 수단을 사용해야 한다. pool 고갈 때의 실패·보존 정책을 명시하고 시험한다.

interrupt 문맥도 별도로 처리한다. `sched_*free`뿐 아니라 직접 `free/mm_free`를 호출하는 경우도 UP/SMP 공통으로 지연 해제한다. malloc 계열은 interrupt에서 일반 heap을 사용하지 못하게 하고, 기존 호출자는 사전 할당 pool이나 task로 작업을 넘기는 방식으로 전환한다. 거절 검사는 `mm_malloc()`의 delay list 처리보다 먼저 해야 한다. signal handler는 task 문맥이므로 이 interrupt 검사만으로 차단되지 않는다.

### 4.2 로그와 진단

MM lock 안에서의 로그는 heap 할당, 압축, 메시지 할당, 외부 callback으로 이어지지 않는 전용 경로로 제한한다. 상세 heap 출력은 사전 확보한 저장소에 필요한 값을 복사한 뒤 lock 밖에서 출력하거나, 검증된 무할당 backend를 사용한다. lock 밖에 생 포인터를 넘기고 나중에 읽는 방식은 허용하지 않는다.

`IS_KMM_LOCKED()`가 `mm_counts_held`를 사용하므로 카운터 삭제 전에 호출자를 전환한다. MM 내부 출력이 할당 backend로 가지 않게 하는 것이 우선이며, 남는 상태 조회는 동기화된 lock 상태 조회를 사용한다. `sem_getvalue()`로 검사한 뒤 malloc하는 방식은 검사와 실행 사이의 경합을 해결하지 못한다.

### 4.3 종료·recovery

정상적인 cancel/termination은 heap 수정 구간이 끝난 뒤 실행하도록 연결한다. 종료 사전 검사인 [prepare_exit()](../../os/kernel/task/task_exit.c#L94)와 MM 소유 상태 출력도 새 lock 계약에 맞춰 검토한다.

중간까지 heap을 수정한 task를 즉시 강제 종료한 경우에는 semaphore만 반환해도 free list의 일관성이 복구되지 않는다. fault/recovery까지 정상 진행시키려면 heap 일관성을 별도로 입증하거나, 소유 app heap 폐기·재초기화 또는 공유 heap 장애 처리 정책이 필요하다. 이번 재귀 제거가 기존 recovery 이슈를 자동 해결한다고 주장하지 않는다.

## 5. 변경 순서와 완료 조건

1. **잠긴 free 코어 + tryfree 도입.** scheduler의 명시적 이중 획득을 제거한다. 같은 소유자의 try는 busy로 처리한다. 아직 남은 blocking 획득의 재귀 지원은 최종 전환 전까지 유지한다.
2. **지연 해제·로그 경로 정리.** 재시도 진행, heap 수명, MM 내부 무할당 출력을 검증한다. 외부 raw try/give wrapper는 남은 참조와 빌드 연결을 확인한 뒤 제거한다.
3. **비동기 실행 정책 구현.** MM 전용 lock의 토큰 부여/반환과 signal·취소 지연을 원자적으로 연결한다. protected 및 SMP 경로를 함께 검증한다.
4. **재귀 기능 삭제.** 모든 지원 경로가 위 계약을 만족한 뒤 `mm_counts_held`와 동일 소유자 우회 획득을 제거한다. 소유자 정보가 진단에 남더라도 실제 획득을 생략하는 근거로 사용하지 않는다.
5. **실패 처리 검증.** 현재 `mm_malloc()`처럼 획득 결과를 무시하는 호출자와 DEBUGASSERT에 획득을 넣은 호출자를 점검한다. 비재귀 획득 실패를 처리하지 않은 채 heap을 수정하거나, 획득하지 않은 lock을 반환해서는 안 된다.

1단계 완료는 정상 해제 호출의 중첩 제거를 뜻한다. 전체 재진입 제거와 `mm_sem` 비재귀화 완료는 3~5단계의 조건까지 만족해야 한다.

주요 변경 위치는 다음과 같다.

| 위치 | 책임 |
| --- | --- |
| `os/mm/mm_heap/mm_free.c`, `mm_sem.c` | 잠긴 해제 코어, tryfree, 비재귀 lock |
| `os/mm/umm_heap`, `os/mm/kmm_heap`, `os/include/tinyara/mm/mm.h`, `kmalloc.h` | heap 선택 wrapper, 선언과 설정별 alias |
| `os/kernel/sched/sched_free.c`, `sched_garbage.c` | 지연 해제 소유권 및 유한한 큐 소비 |
| MM 진단 코드, `os/kernel/log_dump` | lock 안의 무할당 출력과 상태 조회 전환 |
| semaphore/signal/scheduler, 취소·종료 및 아키텍처 signal 전달 코드 | 토큰 부여와 전달 지연의 원자적 연결, protected 진입 경로 |
| `apps/examples/testcase/le_tc` | 중첩 제거·지연 해제·signal 및 종료 회귀 검증 |

## 6. 검증 계획

| 시험 | 성공 기준 |
| --- | --- |
| 일반 free 및 tryfree | 분할·병합·통계 결과 동일, 한 연산당 획득/반환 각각 한 번 |
| 동일 task가 보유한 heap에 tryfree | 즉시 busy, heap 무변경, 정확히 한 번 enqueue 및 최종 free |
| 다른 task의 heap 점유 | tryfree가 기다리지 않음, 반환 후 지연 메모리 회수 |
| interrupt의 직접 free/할당 요청 | UP/SMP 모두 lock 대기·heap 재진입 없음, 지연 해제 또는 명시적 실패 |
| 큐 재시도 | busy 상태에서 무한 반복·중복 enqueue 없음, worker 유무 모두 진행 |
| malloc/realloc/memalign/extend | 데이터 보존, OOM 동작 및 기존 GC 재시도 유지 |
| signal 주입 | 획득 직전/토큰 인계/획득 직후/heap 변경 중/반환 직전·직후에 주입해 중첩 획득·교착·손상 없음 |
| signal 내부 자원 | 고정 pool 고갈과 동적 객체 반환에서 heap 재진입·signal 유실 정책 위반 없음 |
| cancel/exit/recovery | 대기 중 취소 및 보유 중 pending 처리, 토큰 중복 반환·보유 상태 잔존 없음 |
| 로그·heap 진단 | 최대 로그량과 진단 기능 활성화에서도 lock 안 MM 재호출 없음 |
| 빌드 구성 | flat/protected, UMM/KMM, multiheap, UP/SMP, PI 및 recovery 설정별 근거 확보 |

기존 `tc_umm_heap.c`와 `stress_mm_sem_signal.c`를 회귀 기반으로 사용한다. 전환 지점 주입에는 할당하지 않는 시험 hook과 watchdog을 사용한다. 호스트 모델, QEMU, 실제 SMP 보드 검증을 별도로 기록한다. DEBUG 활성·비활성 빌드 모두 확인하며, 실행하지 않은 구성은 지원 검증 완료로 표시하지 않는다.

## 7. 동시에 유지할 수 없는 요구

“heap 수정 도중 handler 즉시 실행”, “handler가 같은 heap의 일반 allocator 호출”, “lock은 비재귀이며 외부 호출은 중단 상태”를 모두 유지하면 self-deadlock이 발생한다. 일반적인 `_locked` 분리나 mutex 교체로 이 세 조건을 동시에 만족시킬 수 없다.

현재 allocator 구조에서는 handler 실행을 지연하거나, handler의 같은 heap 사용 계약을 바꾸거나, 별도의 재진입 가능한 allocator로 교체해야 한다. 권장안은 정상 호출을 먼저 단순화하고 MM 안전 지점으로 비동기 실행을 이동하는 것이다. signal 지연과 종료 정책 변경을 범위에 넣지 않는다면, 제공할 수 있는 결과는 **일반 호출 중첩 제거까지**이며 전체 비재귀화는 완료할 수 없다.
