# NuttX 12.13.0 MM 개발자 사용법 및 마이그레이션 가이드

이 문서는 기존 TizenRT MM 호출자를 유지하면서 allocator를 NuttX 12.13.0으로 전환한 코드의 사용 기준이다. 앱의 일반적인 `malloc/free` 호출은 그대로 사용한다. 시스템 개발자는 heap 수명, 직접 node 접근, 통계 해석과 protected 앱의 종료 처리를 확인해야 한다.

기준 upstream은 `nuttx-12.13.0`, commit `b9b5d9afbf5a88f3c52830d4ce88ae038be0fd35`다. 구조와 원본 관리 규칙은 [모듈 동기화 설계](NuttX_Module_Sync_Design.md), 실행 결과와 증거 범위는 [flat 검증 기록](NuttX_12.13_MM_Validation.md)과 [protected/loadable 검증 기록](NuttX_12.13_MM_Loadable_Validation.md)을 따른다. 이 문서의 API 설명은 현재 구현 기준이며, 기능 설명 자체가 모든 보드·설정에서 실행 검증을 완료했다는 의미는 아니다.

## 1. 대상별 변경 사항

| 대상 | 유지되는 사용법 | 확인하거나 변경할 부분 |
| --- | --- | --- |
| 일반 앱 | `malloc`, `calloc`, `realloc`, `memalign`, `free`와 기존 헤더 | node 내부 주소 계산 제거, 재할당 실패 시 원래 포인터 보존 |
| 커널·드라이버 | `kmm_malloc/free` 등 kernel allocator와 기존 `mm_*` 인자 | 명시적인 heap 선택, IRQ/종료 전환의 지연 해제, 통계 반환값 확인 |
| heapinfo·누수 진단 | caller/PID, heapinfo 기능 | Tizen 진단 node 순회 API 사용, 요청 크기와 물리 사용량 구분 |
| 앱 로더·Binary Manager | 앱별 독립 heap과 common binary의 heap table | 같은 앱의 모든 task/thread 종료 후 정리, 두 mutex 해제 완료 후 RAM 회수 |
| 다음 NuttX 모듈 이식 | 기존 Make 진입점 | imported object 전용 헤더·심볼 경계 유지, compat 계약 단위 교체 |

헤더는 일반 TizenRT 코드에서 `<tinyara/mm/mm.h>`를 사용한다. `<nuttx/mm/mm.h>`와 `trmm_*`, `nxmm_*`는 imported core와 bridge의 내부 계약이다. 앱·드라이버가 이를 직접 호출하는 방식으로 전환하지 않는다.

## 2. 앱의 할당·재할당

표준 allocator의 함수 원형을 바꾸지 않았다. 예를 들어 재할당은 기존 방식으로 작성한다.

```c
#include <stdlib.h>

int resize_buffer(unsigned char **buffer, size_t size)
{
    unsigned char *next;

    if (size == 0) {
        free(*buffer);
        *buffer = NULL;
        return 0;
    }

    next = realloc(*buffer, size);
    if (next == NULL) {
        return -1; /* 기존 *buffer는 계속 호출자 소유다. */
    }

    *buffer = next;
    return 0;
}
```

실패 시에는 반환 포인터를 확인한다. 제품의 allocation-failure 정책인 `CONFIG_MM_ASSERT_ON_FAIL` 등이 활성화되어 있으면 wrapper가 진단 또는 assert를 수행할 수 있으므로 모든 실패가 반드시 `NULL` 반환까지 진행된다고 가정하지 않는다. OOM 테스트 구성도 이 정책을 고려해야 한다.

protected/loadable 앱에서 `malloc`은 현재 앱의 heap을 사용한다. common binary가 여러 앱에 공유되어도 allocation 소유권은 앱별이다. 앱 사이에 heap 포인터를 전달하여 다른 앱에서 해제하는 형태로 사용하지 않는다.

`malloc_at/calloc_at/realloc_at/memalign_at`은 기존 flat multiheap wrapper의 API다. `CONFIG_APP_BINARY_SEPARATION`에서는 이 구현을 제공하지 않는다. `CONFIG_KMM_NHEAPS` 값이 여러 개라는 이유로 loadable 앱에서 kernel heap 번호를 지정할 수 있는 것은 아니다.

## 3. 시스템 코드에서 heap 선택

기존 함수 원형은 [공개 MM 헤더](../os/include/tinyara/mm/mm.h)에 유지한다.

| 용도 | API 및 반환 |
| --- | --- |
| kernel heap 선택 | `struct mm_heap_s *kmm_get_heap_with_index(int index)` — 범위 밖이면 `NULL` |
| kernel 주소의 소유 heap | `struct mm_heap_s *kmm_get_heap(void *address)` — 찾지 못하면 `NULL` |
| 주소의 kernel heap 번호 | `int kmm_get_index_of_heap(void *address)` — 찾지 못하면 `INVALID_HEAP_IDX` |
| 현재 user heap 조회 | `struct mm_heap_s *mm_get_heap_with_index(int index)` — protected 구성에서는 index `0`만 유효 |
| 지정한 app heap에 kernel 측 할당 | `malloc_user_at(heap, size)`, `calloc_user_at`, `memalign_user_at`, `realloc_user_at`, `zalloc_user_at` |
| 지정한 app heap의 포인터 해제 | `void free_user_at(struct mm_heap_s *heap, void *mem)` |

kernel heap은 `kmm_*`로 선택한다. protected kernel의 `BASE_HEAP`는 현재 task의 user heap을 의미하므로 kernel heap 배열로 해석하지 않는다. app heap은 로더에서 확보한 descriptor를 명시적으로 전달한다. heap index는 앱 ID 또는 region index와 다르다. 하나의 heap이 여러 region을 가질 수 있다.

```c
#include <stdlib.h>
#include <tinyara/mm/mm.h>

int inspect_kernel_heap(int heap_index, struct mallinfo *info)
{
    struct mm_heap_s *heap = kmm_get_heap_with_index(heap_index);
    if (heap == NULL) {
        return -1;
    }
    return mm_mallinfo(heap, info);
}
```

이 예는 `CONFIG_MM_KERNEL_HEAP`을 사용하는 kernel 코드용이다. kernel/user가 같은 공개 헤더를 사용하더라도 접근 가능한 heap은 서로 다르다.

## 4. 별도 heap의 생성과 해제

TizenRT의 descriptor를 받는 원형은 유지한다. NuttX 원본의 `mm_initialize(name, start, size)`로 호출자를 바꾸지 않는다.

```c
int mm_initialize(struct mm_heap_s *heap, void *heap_start, size_t heap_size);
int mm_addregion(struct mm_heap_s *heap, void *start, size_t size);
void mm_uninitialize(struct mm_heap_s *heap);

void *mm_malloc(struct mm_heap_s *heap, size_t size, mmaddress_t caller);
void *mm_realloc(struct mm_heap_s *heap, void *old, size_t size,
                 mmaddress_t caller);
void *mm_memalign(struct mm_heap_s *heap, size_t alignment, size_t size,
                  mmaddress_t caller);
void mm_free(struct mm_heap_s *heap, void *mem);
```

`mm_initialize`은 성공 시 `0`, 충분한 backend 공간을 확보하지 못하면 `-ENOMEM`을 반환한다. 공개 descriptor는 호출자 소유이고, NuttX의 private heap control block은 전달한 arena 내부에서 공간을 차지한다. 따라서 같은 크기의 arena라도 이전 allocator와 가용 payload가 같다고 가정하지 않는다.

```c
#include <stdint.h>
#include <tinyara/mm/mm.h>

static struct mm_heap_s work_heap;
static _Alignas(8) unsigned char work_arena[16 * 1024];

int start_work_heap(void)
{
    return mm_initialize(&work_heap, work_arena, sizeof(work_arena));
}

void stop_work_heap(void)
{
    /* 선행 조건: 이 heap의 task/IRQ 사용자를 모두 중지하고,
     * 다른 context가 descriptor나 allocation을 참조하지 않는다. */
    mm_uninitialize(&work_heap);
}
```

`mm_uninitialize`는 지연 해제를 정리하고 backend mutex와 Tizen metadata mutex를 파괴한다. descriptor나 arena 자체를 `free`하는 함수는 아니다. 동적으로 arena를 확보한 소유자는 이 함수가 반환한 뒤 그 arena를 확보한 allocator에 한 번만 반환한다. 해제 후 allocation 포인터를 사용하거나 `mm_free`하지 않는다. 초기화된 heap에 `mm_initialize`를 다시 호출하기 전에도 같은 teardown이 필요하다.

`mm_addregion`은 초기화된 heap, 남은 region capacity, 실제로 소유한 유효한 arena를 요구한다. wrapper는 잘못된 heap 또는 capacity 초과에 `-EINVAL`을 반환한다. region 간 겹침, 시작 주소·크기와 전체 backing memory의 수명은 시스템 호출자가 관리한다. `mm_extend` 역시 region 끝에 연속된 유효 공간을 제공하는 내부 API이며 임의 주소를 추가하는 API가 아니다.

## 5. 직접 node 접근을 바꾸는 방법

공개 `struct mm_heap_s`는 Tizen metadata와 opaque `nuttx_heap` 포인터를 가진다. NuttX free-list/node 구조가 공개 구조체 안에 그대로 들어 있지 않다.

다음과 같은 기존 코드는 수정해야 한다.

- `heap->mm_nodelist`를 직접 순회하는 코드.
- `node + node->size`로 다음 물리 node를 찾는 코드.
- user pointer 앞의 Tizen header를 NuttX header로 cast하는 코드.
- `preceding`의 Tizen allocation bit를 NuttX 원본 node에 적용하는 코드.
- `heap->nuttx_heap`를 다른 모듈에서 NuttX private 구조체로 cast하는 코드.

공개 헤더에서 제거된 내부 함수는 다음과 같이 전환한다. 이름이 비슷한 NuttX 내부 함수를 직접 연결하면 두 allocator의 node layout을 혼용하게 된다.

| 이전 내부 함수 | 전환 방법 |
| --- | --- |
| `mm_shrinkchunk(heap, node, size)` | allocation 소유자가 `mm_realloc`/`realloc`을 호출하고 반환 포인터를 갱신한다. 물리 chunk를 직접 줄이지 않는다. |
| `mm_addfreechunk(heap, node)` | 유효한 allocation은 `mm_free`/`free`로 반환한다. 새 backing region 추가는 `mm_addregion`을 사용한다. |
| `mm_size2ndx(size)` | free-list bucket 선택은 backend가 담당한다. 제품 코드의 bucket 계산을 제거한다. |
| `mm_uaf_poison`, `mm_uaf_verify` | 이번 포트에 대체 provider가 없다. `CONFIG_DEBUG_MM_UAF`는 통합 전까지 지원하지 않는다. |

Tizen allocation 목록은 다음 API로 순회한다. 반환 node는 Tizen 진단 header이며, NuttX 물리 free node나 guard node가 아니다.

```c
struct mm_allocnode_s *mm_first_allocnode(struct mm_heap_s *heap);
struct mm_allocnode_s *mm_next_allocnode(struct mm_allocnode_s *node);
```

```c
#include <errno.h>
#include <tinyara/mm/mm.h>

int count_heap_records(struct mm_heap_s *heap, size_t *count)
{
    struct mm_allocnode_s *node;
    size_t found = 0;

    if (!mm_takesemaphore(heap)) {
        return -EAGAIN;
    }
    for (node = mm_first_allocnode(heap); node != NULL;
         node = mm_next_allocnode(node)) {
        ++found;
    }
    mm_givesemaphore(heap);
    *count = found;
    return 0;
}
```

순회 중에는 heap을 변경하지 않고, 반환 node를 unlock 이후에 보관하지 않는다. 목록에는 안전한 context에서 처리되기 전의 지연 해제 record가 남을 수 있다. 실제 free-list 정보가 필요하면 제품의 `mm_dump_heap_free_node_list` 또는 bridge에 정의된 조회 기능을 사용한다. 일반 모듈에 upstream node 의존성을 다시 추가하지 않는다.

caller/PID 설정은 기존 `heapinfo_set_caller_addr`, `heapinfo_set_pid`, `heapinfo_set_stack_node`를 사용한다. `CONFIG_DEBUG_MM_HEAPINFO`가 필요한 함수인지 공개 헤더의 조건을 확인한다.

## 6. 메모리 사용량과 진단 반환값

요청 크기, Tizen 진단 node 크기, backend가 차지한 물리 chunk 크기는 서로 다르다.

| 값 | 해석 |
| --- | --- |
| `node->size` | 요청 payload 크기 + `SIZEOF_MM_ALLOCNODE`인 Tizen 진단 크기 |
| `mm_allocation_charge(mem)` | 유효한 live Tizen allocation의 NuttX chunk 사용량. 제품 metadata, alignment 여유, allocator overhead 포함 |
| `mallinfo.uordblks` | NuttX backend 기준 사용량. 공개 allocation 합계 외의 control block·guard 등도 포함 |
| `mallinfo.mxordblk` | 가장 큰 물리 free chunk 크기. 같은 크기의 `malloc` 성공을 보장하는 payload 크기는 아님 |
| `mm_mallinfo_aligned(..., alignment)` | 위 통계의 largest 값을 Tizen metadata와 정렬 overhead를 고려하여 보수적으로 조정 |

`mm_allocation_charge`를 호출하는 동안 allocation 소유권을 유지한다. 해제한 포인터의 크기를 알아내는 용도로 사용하지 않는다. 기존 heapinfo per-PID 수치와 backend 총량이 정확히 합산되는지 판단할 때는 두 기준의 차이를 반영한다. 성능이나 가용 메모리가 개선되었다는 주장은 별도 측정이 필요하다.

```c
int mm_mallinfo(struct mm_heap_s *heap, struct mallinfo *info);
int mm_mallinfo_aligned(struct mm_heap_s *heap, struct mallinfo *info,
                       size_t alignment);
int mm_check_heap_corruption(struct mm_heap_s *heap);
```

`mm_mallinfo`는 성공 시 `0`, 출력 포인터가 `NULL`이면 `-EINVAL`, fault/IRQ 등 통계를 안전하게 얻지 못하는 상황이면 `-EAGAIN`을 반환한다. 출력 구조체를 먼저 0으로 초기화한다. 미초기화 heap에 대해 0 통계와 성공을 반환하는 경로가 있으므로 heap 존재 여부를 이 함수로 판단하지 않는다.

`mm_check_heap_corruption`은 검사한 범위가 정상이면 `0`, 손상이 발견되면 `-EFAULT`, lock 사용 중이거나 검사할 수 없는 context이면 `-EAGAIN`을 반환한다. **검사 불가를 heap 손상으로 처리하지 않는다.** 정상 task에서도 검사 시점에 lock이 사용 중이면 재시도나 진단 생략 정책을 적용한다.

fault 경로는 UP에서 IRQ를 막고 두 heap lock이 모두 비어 있을 때만 읽기 전용 검사를 수행한다. 통계나 raw heap dump까지 같은 방식으로 제공하는 것은 아니다. ARM assert 경로도 검사 불가 때문에 이후 crash 진단을 중단하지 않도록 구분한다.

## 7. 잠금과 지연 해제

`mm_takesemaphore(heap)`는 성공 여부를 `bool`로, `mm_trysemaphore(heap)`는 성공 `0` 또는 음수 오류로 반환한다. 획득한 횟수만큼 `mm_givesemaphore`를 호출한다. metadata lock 안에서 backend lock을 사용하는 순서를 유지한다.

두 계층의 mutex는 첫 획득부터 마지막 unlock까지 취소를 비활성화하고, semaphore를 반환한 다음 이전 취소 상태를 복원한다. native provider는 `EINTR`를 재시도하고 호출자의 errno를 보존한다. 호출자가 provider 내부의 semaphore나 owner/count 필드를 직접 수정하지 않는다.

IRQ 또는 task 종료 전환처럼 정상 lock을 획득할 수 없는 context의 free는 지연될 수 있다. protected 구성에서는 kernel과 user가 같은 app heap의 pending head를 공유하므로 양쪽에 같은 lock-free pointer 연산을 사용한다. user 코드에서 IRQ를 직접 마스킹하지 않는다. flat 구성은 IRQ critical section을 유지한다.

해제 요청 직후 통계가 반드시 원상복귀한다고 가정하지 않는다. 종료·unload 검증은 안전한 context의 지연 정리까지 완료된 시점을 기준으로 한다. `free`의 지연 처리가 IRQ에서 `malloc/realloc`의 성공을 보장하지도 않는다.

## 8. Protected/loadable 앱 수명

앱의 main task가 종료되어도 같은 task group의 pthread나 `task_create`로 만든 별도 group의 task가 남아 있으면 heap과 코드/RAM을 유지한다. Binary Manager의 main PID가 사라진 사실만으로 전체 앱의 메모리를 해제해서는 안 된다.

현재 loader 통합의 수명은 다음 순서로 관리한다.

1. `exec_module`이 초기화한 app heap과 `binary_s`를 최초 task group에 연결한다. 별도 child group에 `binary_s` 소유권을 복사하지 않는다.
2. 최초 group의 마지막 member가 나갈 때 binary 소유권을 정리 대기열로 넘긴다. 이 단계에서도 같은 앱의 다른 group 또는 종료 중인 thread가 app RAM을 사용할 수 있다.
3. scheduler의 PID 해제 경계에서 해당 member의 종료 준비를 표시하고 정리 worker를 깨운다. PID가 재사용되기 전에 표시하며, main PID의 이후 조회만으로 앱 종료 여부를 판단하지 않는다.
4. 정리 대기열은 최초 group의 종료 PID가 해제되고, Binary Manager의 thread 목록이 비고, 전체 TCB 중 같은 app ID 또는 heap을 참조하는 task가 없을 때만 unload를 진행한다.
5. 안전한 정리 context가 남은 delayed reference와 app heap 연결을 정리하고 두 heap mutex를 파괴한다.
6. section RAM과 descriptor를 소유한 계층이 한 번씩 반환한다. 완료된 뒤에만 Binary Manager가 재로드 가능한 상태를 공개한다.

Binary Manager에서 남은 앱 member를 선택할 때도 현재 binary의 thread 목록과 group의 binary ID를 확인한다. 예전 main PID를 저장해 두었다가 `sched_gettcb`로 얻은 task를 그대로 종료시키면 재사용된 PID의 다른 task를 종료할 수 있다.

`BIN_HEAP`/`BIN_BSS`가 DATA allocation의 내부 주소인 경우 각 주소를 별도 `kmm_free`하지 않는다. ELF의 통합 allocation과 XIP의 고정 RAM 배치는 소유권이 다르다. section 해제 후 `ramstart`를 다시 해제하는 코드도 추가하지 않는다.

common binary의 app heap table과 app-heap registry를 정리한 후에 backing RAM을 재사용한다. registry의 heap 포인터는 수명 보장 없이 장기간 보관하는 handle이 아니다. `mm_app_heap_registered(heap)`는 등록 여부만 확인하며 heap을 pin하거나 참조 수를 늘리지 않는다.

`mm_lifecycle_snapshot`은 `CONFIG_EXAMPLES_NUTTX_MM_LIFECYCLE && __KERNEL__`에서만 제공되는 검증용 API다. 앱의 production 인터페이스가 아니며, 전체 앱 종료 완료를 알리는 동기화 primitive로 사용하지 않는다.

이번 지원·검증 범위는 앱의 정상 return → 같은 앱의 모든 task/thread 정상 종료 → 정리 완료 → 새 load다. 실행 중인 앱의 강제 unload/update와 fault recovery는 이 범위에 포함되지 않는다. `CONFIG_BINMGR_RECOVERY=n`이어도 Binary Manager의 일반 terminate 경로는 live thread를 강제로 중지할 수 있으므로, 이 설정만으로 강제 종료가 안전해지는 것은 아니다. 중지된 thread가 소유한 새 backend mutex를 복구하는 provider가 먼저 필요하다.

`CONFIG_HEAPINFO_USER_GROUP`의 전역 group 사용량은 현재 `mm_uninitialize`에서 남은 live allocation을 일괄 제거할 때 차감하지 않는다. 따라서 앱이 allocation을 남긴 채 종료하는 경우 해당 전역 통계는 unload 완료나 누수 여부의 근거로 사용할 수 없다. 이번 lifecycle 검증은 이 옵션을 끄고 kernel heap 사용량, registry/table 참조와 실제 mutex teardown 횟수를 확인한다. 이 통계 기능이 필요한 제품은 일괄 teardown 회계 처리를 먼저 추가해야 한다.

## 9. 빌드와 후속 동기화

기존 TizenRT Make 빌드를 유지한다. NuttX의 `CMakeLists.txt`는 원본 추적 대상이며 이 포트의 빌드 진입점이 아니다.

- 원본 allocator: `os/mm/mm_heap/` 및 manifest에 기록된 나머지 upstream 파일.
- 제품 wrapper와 진단: `os/mm/tizenrt/`.
- NuttX private layout 접근: `os/mm/tizenrt/bridge/`.
- 미이식 OS 계약: `os/mm/compat/`.

`compat/include`와 `symbol_map.h`는 imported object와 bridge에만 적용한다. 전역 `CFLAGS`에 추가하면 기존 TizenRT libc/type 선언까지 바뀔 수 있다. protected `libumm`과 `libkmm`은 서로 다른 실행 환경으로 빌드하지만, 공유 app heap의 공개·private layout은 같아야 한다.

후속 이식에서 compat를 제거할 때는 파일 이름보다 다음 계약을 확인한다.

| 현재 호환 처리 | 제거 조건 |
| --- | --- |
| `nuttx_os.c` mutex·task·IRQ provider | 새 provider가 취소·errno·boot/task-exit·protected syscall 계약을 제공하고 검증됨 |
| private symbol/header projection | 충돌하는 기존 공개 ABI의 호출자 전환 완료, 중복 provider 없이 링크됨 |
| imported `memcpy`의 `memmove` 매핑 | backward realloc의 겹치는 범위 복사가 upstream에서 해결되고 회귀 검증됨 |
| `trmm_extend` 사용량 보정 | upstream extension accounting 수정 확인, 중복 보정 제거 후 회귀 검증됨 |
| protected pending-free 원자 head | 새 lifetime/deferred-free provider가 kernel/user 공유 producer와 consumer를 함께 대체함 |
| Tizen allocation metadata | heapinfo/PID/caller 요구사항과 모든 직접 참조 호출자를 대체하기 전까지 유지 |

원본 동기화는 `tools/nuttx-sync/verify.py`, allocator/호환 동작은 `os/mm/compat/tests/run.sh`로 확인한다. 원본 checksum 일치와 host 테스트 통과는 firmware 링크·MPU·앱 unload 검증을 대신하지 않는다.

## 10. 적용 순서와 검증 범위

일반 앱은 표준 할당 API를 유지하고, 시스템·진단 코드부터 다음 순서로 옮긴다.

1. 물리 node/free-list 직접 접근을 검색하여 Tizen 순회 API 또는 bridge로 변경한다.
2. kernel heap index와 현재 앱 heap을 구분하고 음수·상한 index를 검사한다.
3. `mm_mallinfo`와 corruption 검사 반환값을 처리한다.
4. 사용자 중지 → 지연 해제 정리 → 두 mutex teardown → backing RAM 회수 순서를 확인한다.
5. protected common/user/kernel 이미지를 같은 헤더와 설정으로 다시 빌드한다. layout이 달라지는 이전 prebuilt 앱·common binary를 혼합하지 않는다.
6. 최종 firmware로 TASH allocation 검사와 main-first/last-pthread/별도 task group 종료, 재로드, heap 사용량 복귀를 검증한다.

예제 옵션을 켠 이미지의 TASH 명령은 다음과 같다. lifecycle 명령은 검증용 loadable 앱과 kernel driver도 함께 구성해야 하므로 [예제 준비 방법](../apps/examples/nuttx_mm_test/LIFECYCLE.md)을 먼저 따른다.

| 빌드 구성 | 해당 이미지의 TASH 명령 |
| --- | --- |
| flat `qemu-armv8m/hello` | `nuttx_mm_test` |
| protected `qemu-armv8m/loadable_all` 또는 `loadable_apps` | `nuttx_mm_life` |

flat UP QEMU 결과는 [기존 검증 기록](NuttX_12.13_MM_Validation.md)에 있다. protected/loadable 실행 결과와 정확한 설정·소스·패키지 hash는 [별도 검증 기록](NuttX_12.13_MM_Loadable_Validation.md)을 따른다. `mm_nuttx_backend_version()`과 `mm_nuttx_backend_revision()`으로 연결된 backend의 버전과 고정 SHA를 확인할 수 있다. 이 문자열 확인만으로 모든 설정의 실행 검증을 대신하지 않는다.

SMP, 다른 보드, 실제 panic/crash dump, 모든 debug 옵션, 최대 가용 메모리·성능 비교는 별도 검증 항목이다. `KASAN/TLSF/mempool`, `GRAN/PGALLOC/MMU`, free-history/UAF, large-back placement, NuttX heap trace/backtrace, `MM_DUMP_CORRPUTED_HEAP`, NuttX dump/panic-on-failure 및 Binary Manager 강제 복구 등 명시적으로 거부하는 옵션은 필요한 provider를 먼저 구현해야 한다. `BINFMT_LOADABLE`과 `BINFMT_CONSTRUCTORS`의 조합도 정리 worker에서 앱 context를 설정하여 destructor를 실행하는 provider가 없어 빌드 단계에서 거부한다.
