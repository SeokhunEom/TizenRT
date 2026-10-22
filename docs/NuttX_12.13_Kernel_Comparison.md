# TizenRT와 Apache NuttX 12.13.0 커널 비교

조사일: 2026-10-22. 사용자 지정 이식 기준인 **12.13.0**의 source를 직접 비교했다. 이전 13.1 문서의 버전 번호만 변경한 자료가 아니다.

## 기준과 읽는 방법

| 대상 | 고정 commit |
| --- | --- |
| TizenRT 이식 전 `master` | `94a61e7e2998ef4c4f77331d13b31986ec97b726` |
| NuttX `nuttx-12.13.0` | `b9b5d9afbf5a88f3c52830d4ce88ae038be0fd35` |

이 문서는 **이식 전 TizenRT와 고정 NuttX의 차이**다. 후속 구현에서 바뀐 현재 상태와 혼동하지 않는다. TizenRT 파일은 `git show <TizenRT SHA>:<path>`, NuttX 파일은 아래 고정 링크 또는 `git show <NuttX SHA>:<path>`로 재현할 수 있다. QEMU 실행 결과는 이 소스 비교 자체가 아니라 별도 검증 기록이 제공한다.

## 모듈별 차이

| 영역 | TizenRT 기준선 | NuttX 12.13.0 | 이식 시 경계 |
| --- | --- | --- | --- |
| 빌드 | `os/`, `lib/libc`, Make/Kconfig/dbuild | `sched/`, `libs/libc`, Make와 CMake | 기존 TizenRT 빌드를 유지하고 모듈 source 선택만 연결 |
| heap API | 공개 heap layout, 기존 heap 객체 초기화, caller 인자 | private heap, `mm_initialize_heap(config)`, heap 포인터 반환 | 초기화 소유권과 wrapper 규약 변경 |
| node 상태 | `preceding`의 상위 allocation bit, 제품 metadata | `size` 하위 allocation/previous-free bits, 선택적 backtrace | heapinfo·누수 검사에 기존 해석 사용 불가 |
| heap 동기화 | semaphore와 TizenRT 종료/지연 free 처리 | `mutex_t`, per-heap/per-CPU delaylist, IRQ/context 판정 | 임시 adapter로 동작 계약 보존 |
| TCB | watchdog 포인터, TCB errno/cancel count, 앱 heap 확장 | 내장 watchdog, `errcode`, TLS errno/cancel count | 생성·종료·libc·Binary Manager가 연결됨 |
| SMP scheduler | CPU별 assigned queue, block/unblock 경계 | CPU별 TCB 포인터와 delivery 상태 | 같은 전역 이름도 타입·의미가 다름 |
| semaphore | 16-bit count, 전역 waiter 구조와 앱 복구 확장 | 32-bit count/mutex-holder union, 객체별 waitlist, `nxsem`/`nxmutex` | 오류 반환, 취소, owner/PI, 앱 복구 포함 |
| clock | 설정에 따른 unsigned 32/64-bit clock | **설정에 따른 unsigned 32/64-bit clock** | 13.1의 signed 64-bit 설명을 적용하면 안 됨 |
| watchdog | 동적 객체, variadic callback, 상대 lag | 내장 객체, 단일 `wdparm_t`, absolute `expired` | 타입이 유사해도 소유권과 timeout 계산이 다름 |
| workqueue | 기존 dq/qtime/delay 및 비동기 cancel | `list_node`, 동적 queue, `work_cancel_sync` | callback 수명과 cancel 동기화 검토 |
| VFS | 고정 file 배열과 semaphore | fd와 file 객체 분리, `atomic_t f_refs`, 분할 fd 배열 | open/dup/close 및 task group 종료 영향 |
| network | lwIP와 netmgr/Wi-Fi 제품 기능 | NuttX 자체 network stack | 별도 stack 전환 과제 |

TizenRT에도 SMP·PI·보호 빌드가 이미 있다. 기능 유무보다 자료구조와 생명주기 계약의 차이가 핵심이다. NuttX 최신 구조를 가져왔다는 이유만으로 성능·안정성 개선을 단정하지 않는다.

## mm 상세 차이

```c
/* 이식 전 TizenRT */
int mm_initialize(struct mm_heap_s *heap, void *start, size_t size);
void *mm_malloc(struct mm_heap_s *heap, size_t size, mmaddress_t caller);

/* NuttX 12.13.0 */
struct mm_heap_s *mm_initialize_heap(const struct mm_heap_config_s *config);
/* mm_initialize(name, start, size)는 위 함수를 호출하는 inline 함수 */
void *mm_malloc(struct mm_heap_s *heap, size_t size);
```

`mm_heap_config_s`는 `heap`, `name`, `start`, `size`, `nokasan`을 가진다. `heap == NULL`이면 arena 안에서 heap control block을 구성한다. 따라서 기존 wrapper의 외부 `struct mm_heap_s` 메모리를 그대로 upstream에 넘기는 방식은 성립하지 않는다.

NuttX의 `mm_lock()`은 UP IRQ에서 mutex가 잠겨 있으면 `-EAGAIN`, SMP IRQ이면 항상 `-EAGAIN`, `_SCHED_GETTID() < 0`이면 `-ESRCH`를 반환한다. 이 반환은 지금 해제할 수 없는 메모리를 deferred list로 넘기는 데 사용한다. 잠금을 항상 성공시키는 adapter는 유효한 구현이 아니다.

기존 TizenRT 앱 로더의 heap 크기/주소 계산, `mm_get_heap`의 region 검색, heapinfo·누수 검사와 종료 시 deferred free는 제품 통합 범위다. 기본 allocator 소스의 전면 수정 대신 추가 wrapper와 bridge로 연결하되, 외부의 직접 layout 접근은 필요한 범위에서 변경한다.

## 다른 커널 모듈을 먼저 이식할 필요가 있는가

`mm`부터 진행할 수 있다. mutex·IRQ·task 조회 중 기본 allocator가 요구하는 interface만 호환 계층에 제공한다. 이를 위해 scheduler/semaphore 전체를 먼저 가져오는 것은 선행 조건이 아니다. 다만 지금 커널에서 올바르게 제공할 수 없는 특정 계약이 발견되면 그 경로의 최소 변경은 필요하다.

전체 `mm/` source 동기화와 모든 allocator/옵션 runtime 지원은 별도로 기록한다. 이번 설계와 커밋 순서는 [모듈 동기화 설계](NuttX_Module_Sync_Design.md)가 기준이다. 구현 후 원본 차이, 남은 호환 처리와 실제 TASH 결과는 [최종 검증 기록](NuttX_12.13_MM_Validation.md)에 있다.

## 고정 소스 근거

- TizenRT `os/include/tinyara/mm/mm.h`, `os/mm/mm_heap`, `os/binfmt/binfmt_execmodule.c`, `os/kernel/debug/mem_leak_checker.c`.
- NuttX [mm public API](https://github.com/apache/nuttx/blob/b9b5d9afbf5a88f3c52830d4ce88ae038be0fd35/include/nuttx/mm/mm.h), [private layout](https://github.com/apache/nuttx/blob/b9b5d9afbf5a88f3c52830d4ce88ae038be0fd35/mm/mm_heap/mm.h), [initialization](https://github.com/apache/nuttx/blob/b9b5d9afbf5a88f3c52830d4ce88ae038be0fd35/mm/mm_heap/mm_initialize.c), [lock](https://github.com/apache/nuttx/blob/b9b5d9afbf5a88f3c52830d4ce88ae038be0fd35/mm/mm_heap/mm_lock.c).
- NuttX [TCB](https://github.com/apache/nuttx/blob/b9b5d9afbf5a88f3c52830d4ce88ae038be0fd35/include/nuttx/sched.h), [TLS](https://github.com/apache/nuttx/blob/b9b5d9afbf5a88f3c52830d4ce88ae038be0fd35/include/nuttx/tls.h), [scheduler queues](https://github.com/apache/nuttx/blob/b9b5d9afbf5a88f3c52830d4ce88ae038be0fd35/sched/sched/sched.h).
- NuttX [semaphore layout](https://github.com/apache/nuttx/blob/b9b5d9afbf5a88f3c52830d4ce88ae038be0fd35/include/semaphore.h), [internal semaphore API](https://github.com/apache/nuttx/blob/b9b5d9afbf5a88f3c52830d4ce88ae038be0fd35/include/nuttx/semaphore.h).
- NuttX [clock types](https://github.com/apache/nuttx/blob/b9b5d9afbf5a88f3c52830d4ce88ae038be0fd35/include/sys/types.h), [watchdog](https://github.com/apache/nuttx/blob/b9b5d9afbf5a88f3c52830d4ce88ae038be0fd35/include/nuttx/wdog.h), [workqueue](https://github.com/apache/nuttx/blob/b9b5d9afbf5a88f3c52830d4ce88ae038be0fd35/include/nuttx/wqueue.h), [VFS](https://github.com/apache/nuttx/blob/b9b5d9afbf5a88f3c52830d4ce88ae038be0fd35/include/nuttx/fs/fs.h).
