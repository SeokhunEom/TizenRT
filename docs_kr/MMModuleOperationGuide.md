# MM 모듈 작동 안내서

이 문서는 TizenRT의 `os/mm` 모듈이 heap을 만들고, 할당하고, 해제하는 흐름을 설명한다. 기준 defconfig는 `build/configs/rtl8730e/loadable_ext_ddr_st7785/defconfig`이다.

목표는 두 가지이다.

- 현재 `rtl8730e/loadable_ext_ddr_st7785` 설정에서 MM이 어떤 메모리 구획을 heap으로 쓰는지 이해한다.
- 나중에 heap 크기, loadable app RAM, multi-heap, debug 옵션을 수정할 때 어느 파일과 흐름을 봐야 하는지 알 수 있게 한다.

## 목차

- [구성 기준](#configuration-baseline)
- [소스 맵](#source-map)
- [런타임 힙 토폴로지](#runtime-heap-topology)
- [부팅 시 초기화](#boot-time-initialization)
- [핵심 할당자 데이터 구조](#core-allocator-data-structures)
- [할당 경로](#allocation-path)
- [자유 경로](#free-path)
- [KMM 및 UMM 래퍼](#kmm-and-umm-wrappers)
- [로드 가능한 앱 힙 흐름](#loadable-app-heap-flow)
- [디버그 및 실패 처리](#debug-and-failure-handling)
- [파이프라인을 수정하는 방법](#how-to-modify-the-pipeline)
- [일반적인 함정](#common-pitfalls)

<a id="configuration-baseline"></a>
## 구성 기준선

`rtl8730e/loadable_ext_ddr_st7785`는 flat build가 아니라 protected + loadable app 구성을 사용한다. MM 관점에서 중요한 값은 다음과 같다.

| 설정 | 값 | MM 관점의 의미 |
| --- | --- | --- |
| `CONFIG_BUILD_PROTECTED` | `y` | kernel 영역과 user/app 영역을 분리한다. |
| `CONFIG_BUILD_2PASS` | `y` | protected build의 kernel/user 산출물 생성을 전제로 한다. |
| `CONFIG_ARCH_USE_MMU` | `y` | address environment와 MMU 기반 보호/매핑을 사용한다. |
| `CONFIG_RAM_DDR` | `y` | board `Make.defs`에서 DDR용 linker script인 `rlx8730e_img2_ddr.ld`를 선택한다. |
| `CONFIG_RAM_START` / `CONFIG_RAM_VSTART` | `0x60100000` | CA32 non-secure DDR 가상/시작 주소이다. |
| `CONFIG_RAM_SIZE` | `66060288` | 63 MiB DDR 영역이다. 끝 주소는 `0x64000000`이다. |
| `CONFIG_RAM_KREGIONx_START` | `"0x60100000,"` | kernel heap region 후보의 시작 주소이다. |
| `CONFIG_RAM_KREGIONx_SIZE` | `"66060288,"` | kernel heap region 후보의 크기이다. |
| `CONFIG_MM_KERNEL_HEAP` | `y` | kernel heap을 user heap과 별도로 둔다. |
| `CONFIG_KMM_REGIONS` | `1` | kernel heap region은 1개이다. |
| `CONFIG_KMM_NHEAPS` | `1` | kernel heap instance는 1개이다. |
| `CONFIG_APP_BINARY_SEPARATION` | `y` | loadable app마다 app heap을 따로 만든다. |
| `CONFIG_SUPPORT_COMMON_BINARY` | `y` | common binary가 app heap table을 통해 app heap 포인터를 공유한다. |
| `CONFIG_COMMON_BIN_STATIC_RAMSIZE` | `524288` | common binary 정적 RAM 예산은 512 KiB이다. |
| `CONFIG_NUM_APPS` | `1` | app heap table에서 app id 1만 실질적으로 사용한다. |
| `CONFIG_APP1_BIN_DYN_RAMSIZE` | `5242880` | app1의 동적 RAM 예산은 5 MiB이다. 실제 heap은 data/bss와 `struct mm_heap_s`를 뺀 나머지이다. |
| `CONFIG_BINFMT_ENABLE` / `CONFIG_BINFMT_LOADABLE` | `y` | loadable binary loader를 사용한다. |
| `CONFIG_XIP_ELF` | `y` | ELF text는 flash/XIP 영역에 두고 RAM에는 data/bss/heap을 둔다. |
| `CONFIG_DEBUG_MM` | `y` | MM debug 로그가 켜져 있다. |
| `CONFIG_DEBUG_MM_HEAPINFO` | `y` | allocation caller, pid, peak/current size 추적 필드가 node에 추가된다. |
| `CONFIG_DEBUG_MM_FREEINFO` | `y` | free caller, free pid 추적 필드가 free node에 추가된다. |
| `CONFIG_MM_ASSERT_ON_FAIL` | `y` | allocation failure 처리에서 assert 성격의 fail-fast 동작을 기대해야 한다. |

링커 관점에서는 `build/configs/rtl8730e/Make.defs`가 `CONFIG_RAM_DDR=y`일 때 `build/configs/rtl8730e/scripts/rlx8730e_img2_ddr.ld`를 선택한다. 이 linker script에는 `CA32_BL3_DRAM_NS`가 `0x60100000`부터 `0x64000000`까지로 정의되어 있고, defconfig의 `CONFIG_RAM_*` 값도 같은 DDR 범위를 가리킨다.

<a id="source-map"></a>
## 소스 맵

MM 동작을 볼 때 주로 보는 파일은 다음과 같다.

| 영역 | 파일 | 역할 |
| --- | --- | --- |
| 공통 allocator | `os/mm/mm_heap/mm_initialize.c` | heap 초기화, region 추가, guard node 생성 |
| 공통 allocator | `os/mm/mm_heap/mm_malloc.c` | best-fit 기반 allocation |
| 공통 allocator | `os/mm/mm_heap/mm_free.c` | free, double-free 검사, 인접 free chunk 병합 |
| 공통 allocator | `os/mm/mm_heap/mm_addfreechunk.c` | free chunk를 크기별 free-list bin에 삽입 |
| 공통 allocator | `os/mm/mm_heap/mm_getheap.c` | 주소가 속한 app/kernel heap 찾기 |
| 공통 allocator | `os/mm/mm_heap/mm_sem.c` | heap별 semaphore와 re-entrant lock 관리 |
| 공통 header | `os/include/tinyara/mm/mm.h` | node layout, heap 구조체, `BASE_HEAP` 선택 매크로 |
| kernel heap wrapper | `os/mm/kmm_heap/*.c` | `kmm_malloc`, `kmm_free`, `kmm_initialize` 등 |
| user/app heap wrapper | `os/mm/umm_heap/*.c` | `malloc`, `free`, `umm_initialize` 등 |
| boot init | `os/kernel/init/os_start.c` | boot 중 kernel heap 초기화와 app heap queue 초기화 |
| arch heap provider | `os/arch/arm/src/common/up_allocateheap.c` | `CONFIG_RAM_KREGIONx_*` 기반 kernel heap 범위 제공 |
| board build config | `build/configs/rtl8730e/Make.defs` | DDR linker script 선택 |
| DDR linker script | `build/configs/rtl8730e/scripts/rlx8730e_img2_ddr.ld` | DDR/flash memory map 정의 |
| binary descriptor | `os/include/tinyara/binfmt/binfmt.h` | `struct binary_s`, `BIN_HEAP` section 정의 |
| loadable execution | `os/binfmt/binfmt_execmodule.c` | app heap object 배치와 `mm_initialize()` 호출 |
| XIP ELF loader | `os/binfmt/libxipelf/xipelf.c` | XIP ELF의 data/bss/heap section 계산 |
| non-XIP ELF addrenv | `os/binfmt/libelf/libelf_addrenv.c` | 일반 ELF의 RAM partition과 heap 계산 |
| binary manager | `os/kernel/binary_manager/binary_manager_internal.h` | binary별 RAM size, stack, heap pointer macro |

<a id="runtime-heap-topology"></a>
## 런타임 힙 토폴로지

이 defconfig의 MM topology는 다음처럼 나눠서 보는 것이 좋다.

```
DDR 0x60100000 .. 0x64000000
|
+-- kernel heap region 후보
|   CONFIG_RAM_KREGIONx_START="0x60100000,"
|   CONFIG_RAM_KREGIONx_SIZE ="66060288,"
|   CONFIG_KMM_REGIONS=1
|   CONFIG_KMM_NHEAPS=1
|
+-- loadable app RAM partition
    binary manager / XIP ELF loader가 app별 data, bss, heap을 배치한다.
    app1 동적 RAM 예산: CONFIG_APP1_BIN_DYN_RAMSIZE=5242880
```

중요한 점은 `CONFIG_APP_BINARY_SEPARATION=y`이므로 app heap은 boot 시점의 일반 user heap으로 한 번 만들어지는 것이 아니라, app binary가 load/exec될 때 `BIN_HEAP` 영역 안에 만들어진다는 것이다.

kernel heap은 `g_kmmheap[CONFIG_KMM_NHEAPS]`에 allocator 상태를 보관한다. 이 구성에서는 `CONFIG_KMM_NHEAPS=1`이므로 `g_kmmheap[0]`만 사용한다.

app heap은 loadable app RAM 안에 `struct mm_heap_s`를 먼저 놓고, 그 뒤의 공간을 allocator region으로 초기화한다. kernel은 app heap queue를 통해 app heap들을 추적하고, user/common binary 쪽은 app heap table을 통해 현재 app heap을 찾는다.

<a id="boot-time-initialization"></a>
## 부팅 시간 초기화

boot 중 MM 초기화의 큰 흐름은 `os/kernel/init/os_start.c`에서 시작한다.

1. scheduler 기본 자료구조와 semaphore subsystem을 초기화한다.
2. `CONFIG_MM_KERNEL_HEAP=y`이므로 `up_allocate_kheap(&heap_start, &heap_size)`를 호출한다.
3. `kmm_initialize(heap_start, heap_size)`를 호출한다.
4. `kmm_initialize()`는 `mm_initialize(kmm_get_baseheap(), heap_start, heap_size)`로 바로 위임한다.
5. `mm_initialize()`가 `struct mm_heap_s` 내부 필드를 초기화하고 `mm_addregion()`으로 첫 region을 추가한다.
6. `up_add_kregion()`을 호출한다. 이 defconfig는 `CONFIG_KMM_REGIONS=1`이므로 추가 region 루프는 빌드되지 않는다.
7. `CONFIG_APP_BINARY_SEPARATION=y`이므로 `mm_initialize_app_heap_q()`를 호출해서 kernel-side app heap queue를 초기화한다.
8. 이후 task/group/driver 초기화로 넘어간다.

`up_allocate_kheap()`은 `CONFIG_RAM_KREGIONx_START/SIZE`에서 파생된 `KREGION_START`, `KREGION_END`를 기준으로 kernel heap 후보 범위를 정한다. ARM common 구현은 idle stack이 해당 region 안에 걸려 있으면 heap start를 stack 끝 이후로 조정하고, 그렇지 않으면 `KREGION_START`부터 heap으로 사용한다.

현재 설정값만 놓고 보면 후보 범위는 다음과 같다.

```
KREGION_START = 0x60100000
KREGION_SIZE  = 66060288
KREGION_END   = 0x64000000
```

실제 heap start는 boot 시점의 idle stack 배치에 따라 `0x60100000` 또는 stack 끝 이후 주소가 될 수 있다. 따라서 runtime 로그나 map을 분석할 때는 defconfig 값만 보지 말고 `up_allocate_kheap()`의 조정 로직도 같이 봐야 한다.

### 지역등록

`mm_initialize()`가 호출되면 allocator가 바로 malloc 가능한 상태가 되는 것은 `mm_addregion()`이 최초 free node를 만들어 주기 때문이다.

`mm_addregion()`의 작업은 다음 순서이다.

1. `heapstart`와 `heapsize`가 유효한지 확인한다.
2. `heapstart`를 `MM_ALIGN_UP()`하고, `heapstart + heapsize`를 `MM_ALIGN_DOWN()`해서 allocator alignment에 맞춘다.
3. 정렬 후에도 usable size가 남아 있는지 확인한다.
4. `heap->mm_heapsize`에 region 크기를 더한다.
5. region 시작 지점에 allocated 상태의 head guard node를 만든다.
6. region 끝 지점에 allocated 상태의 tail guard node를 만든다.
7. 두 guard node 사이 전체를 하나의 큰 free node로 만든다.
8. 그 free node를 `mm_addfreechunk()`로 free-list bin에 넣는다.

head/tail guard node는 allocator가 region 밖으로 병합하거나 탐색하지 못하게 하는 경계 표식이다. 실제 app/kernel allocation은 guard node 사이의 free node에서만 나온다.

<a id="core-allocator-data-structures"></a>
## 코어 할당자 데이터 구조

공통 allocator는 `struct mm_heap_s` 하나를 독립 heap 하나로 본다. heap이 여러 개이면 `struct mm_heap_s` 배열을 여러 개 두는 방식이다.

`struct mm_heap_s`의 핵심 필드는 다음과 같다.

- `mm_semaphore`, `mm_holder`, `mm_counts_held`: heap metadata를 보호하는 lock이다. 같은 PID가 다시 잡을 수 있게 count를 둔다.
- `mm_heapsize`: 이 heap에 등록된 전체 region 크기의 합이다.
- `mm_heapstart[]`, `mm_heapend[]`: region별 앞/뒤 guard node 주소이다.
- `mm_nodelist[]`: free chunk를 크기 class별로 묶는 free-list bin array이다.
- `mm_delaylist[]`: 즉시 free할 수 없는 상황에서 delayed free node를 임시로 보관한다.
- `alloc_list`, `total_alloc_size`, `peak_alloc_size`: `CONFIG_DEBUG_MM_HEAPINFO`가 켜졌을 때만 의미가 있다.

allocator의 물리 chunk는 항상 header를 포함한다.

```
allocated chunk:
+-----------------------+------------------+
| struct mm_allocnode_s | user payload      |
+-----------------------+------------------+

free chunk:
+-------------------------------------------+
| struct mm_freenode_s                       |
| - mm_allocnode_s 공통 필드                 |
| - flink/blink                              |
| - free debug info, if enabled              |
+-------------------------------------------+
```

`struct mm_allocnode_s`의 중요한 필드는 다음과 같다.

- `preceding`: 바로 앞 chunk 크기를 저장한다. 동시에 `MM_ALLOC_BIT`를 통해 현재 chunk가 allocated 상태인지 표현한다.
- `size`: 현재 chunk 전체 크기이다. payload만이 아니라 header를 포함한다.
- `alloc_call_addr`, `pid`, `memory_state`: heapinfo debug가 켜졌을 때 allocation 추적에 사용한다.

`struct mm_freenode_s`는 free-list에 연결되어야 하므로 `flink`, `blink`를 추가로 가진다. `CONFIG_DEBUG_MM_FREEINFO=y`에서는 `free_call_addr`, `free_call_pid`도 가진다.

현재 `mm.h`는 `MM_MIN_SHIFT`를 4로 고정한다. 따라서 allocator의 최소 정렬 단위는 `MM_MIN_CHUNK = 16` bytes이다. `mm_addregion()`은 heap start를 `MM_ALIGN_UP()`하고 heap end를 `MM_ALIGN_DOWN()`해서 region을 이 단위에 맞춘다.

<a id="allocation-path"></a>
## 할당 경로

공통 allocation은 `mm_malloc(struct mm_heap_s *heap, size_t size, mmaddress_t caller_retaddr)`에서 수행한다.

흐름은 다음과 같다.

1. `mm_free_delaylist(heap)`로 이전에 미뤄둔 free 요청을 먼저 처리한다.
2. 요청 크기가 allocator header를 더했을 때 표현 가능한 범위를 넘는지 확인한다.
3. 요청 크기에 `SIZEOF_MM_ALLOCNODE`를 더하고 `MM_ALIGN_UP()`으로 16-byte 경계에 맞춘다.
4. `mm_takesemaphore(heap)`로 heap lock을 잡는다.
5. `mm_size2ndx(size)`로 시작 bin index를 계산한다.
6. 해당 bin부터 더 큰 bin까지 검색해 요청 크기 이상인 free node를 찾는다.
7. 같은 bin 안에서는 크기 내림차순 list를 순회해 best-fit에 가까운 node를 선택한다.
8. 선택한 free node를 free-list에서 제거한다.
9. 남는 공간이 `SIZEOF_MM_FREENODE` 이상이면 allocated chunk와 remainder free chunk로 split한다.
10. allocated chunk의 `preceding`에 `MM_ALLOC_BIT`를 세운다.
11. heapinfo debug가 켜져 있으면 caller address, pid, current/peak size를 갱신한다.
12. header 뒤 주소를 user pointer로 반환한다.

할당 실패 시에는 lock을 놓고 `sched_garbagecollection()`을 한 번 수행한 뒤 재시도한다. 그래도 실패하면 wrapper 쪽에서 `mm_manage_alloc_fail()`로 실패 정보를 처리한다. 이 defconfig는 `CONFIG_MM_ASSERT_ON_FAIL=y`이므로 allocation failure가 단순 `NULL` 반환보다 강하게 드러날 수 있다.

<a id="free-path"></a>
## 무료 경로

표준 `free()`나 `kmm_free()`는 직접 free-list를 만지지 않고, 먼저 주소가 속한 heap을 찾은 다음 `mm_free(heap, mem)`로 들어간다.

`mm_free()` 내부 흐름은 다음과 같다.

1. `mem == NULL`이면 debug 로그만 남기고 반환한다.
2. `mm_takesemaphore(heap)`가 실패하면 delayed free list에 넣고 반환한다. SMP interrupt context처럼 semaphore를 기다릴 수 없는 상황을 위한 경로이다.
3. user pointer에서 `SIZEOF_MM_ALLOCNODE`만큼 뒤로 가서 chunk header를 찾는다.
4. `MM_ALLOC_BIT`가 없으면 이미 free된 pointer이거나 heap 밖 임의 주소로 판단하고 warning을 출력한다.
5. heapinfo/freeinfo debug 정보를 갱신한다.
6. 현재 chunk의 allocated bit를 지운다.
7. 다음 물리 chunk가 free 상태이면 free-list에서 제거하고 현재 chunk와 병합한다.
8. 이전 물리 chunk가 free 상태이면 free-list에서 제거하고 현재 chunk와 병합한다.
9. 병합된 chunk를 `mm_addfreechunk()`로 free-list bin에 다시 넣는다.
10. heap lock을 해제한다.

이 설계 때문에 free 시점에는 인접 free chunk가 즉시 병합된다. 장기적으로 작은 free chunk가 남는 fragmentation은 줄어들지만, 서로 다른 heap 사이에서는 절대 병합되지 않는다.

<a id="kmm-and-umm-wrappers"></a>
## KMM 및 UMM 래퍼

`os/mm/mm_heap`의 allocator는 heap 종류를 모른다. heap 종류를 나누는 것은 wrapper와 `BASE_HEAP` 선택 로직이다.

### 커널 힙

kernel heap wrapper는 `os/mm/kmm_heap`에 있다.

- `g_kmmheap[CONFIG_KMM_NHEAPS]`가 kernel heap 상태 배열이다.
- `kmm_initialize()`는 `mm_initialize()`로 위임한다.
- `kmm_malloc()`은 `g_kmmheap` 배열을 순서대로 시도한다. 이 defconfig는 heap이 1개라 `g_kmmheap[0]`만 대상이다.
- `kmm_free()`는 `kmm_heapmember(mem)`로 kernel heap pointer인지 확인한 뒤 `mm_free()`를 호출한다.

kernel 내부 객체, TCB, scheduler 구조, app heap queue node처럼 kernel이 소유해야 하는 메모리는 `kmm_malloc()`/`kmm_zalloc()` 계열로 잡는다.

### User/app 힙

user heap wrapper는 `os/mm/umm_heap`에 있다. 표준 C allocation 함수도 여기에서 제공한다.

`CONFIG_APP_BINARY_SEPARATION=y`일 때 `malloc()`은 다음처럼 동작한다.

1. user/common binary 코드에서 `BASE_HEAP`은 app heap table을 본다.
2. `CONFIG_NUM_APPS=1`이므로 `BASE_HEAP`은 `g_app_heap_table[1]`로 고정된다.
3. `malloc(size)`는 `mm_malloc(BASE_HEAP, size, caller_retaddr)`를 호출한다.

`free(mem)`은 `mm_get_heap(mem)`으로 주소가 속한 heap을 찾은 뒤 `mm_free()`를 호출한다. kernel build 쪽에서 `mm_get_heap()`은 app heap queue를 먼저 검색하고, 해당 app heap이 아니면 kernel heap 범위를 검색한다.

실무 규칙은 단순하다.

- `kmm_malloc()`으로 받은 pointer는 `kmm_free()`로 해제한다.
- `malloc()`으로 받은 pointer는 `free()`로 해제한다.
- app/user heap pointer를 kernel heap API에 넘기거나, kernel heap pointer를 user heap API에 넘기지 않는다.

<a id="loadable-app-heap-flow"></a>
## 로드 가능한 앱 힙 흐름

이 defconfig의 핵심은 loadable app heap이다. `CONFIG_XIP_ELF=y`이므로 text는 XIP 가능한 flash 영역에 있고, RAM에는 data/bss/heap이 잡힌다.

### Loader가 heap section을 계산한다

`struct binary_s`는 `sections[BIN_MAX]`와 `sizes[BIN_MAX]`를 가진다. 그중 `BIN_HEAP`이 app heap 영역이다.

XIP ELF 경로에서는 `os/binfmt/libxipelf/xipelf.c`가 userspace metadata에서 다음 값을 채운다.

- `sections[BIN_DATA]`: RAM data 시작 주소
- `sizes[BIN_DATA]`: RAM data 크기
- `sections[BIN_BSS]`: bss 시작 주소
- `sizes[BIN_BSS]`: bss 크기
- `sections[BIN_HEAP]`: userspace heap 시작 주소
- `sizes[BIN_HEAP]`: `heap_end - heap_start - sizeof(struct mm_heap_s)`

즉 `BIN_HEAP` 전체의 맨 앞에는 `struct mm_heap_s`가 들어가고, 실제 allocator가 나눠 줄 수 있는 payload region은 그 뒤에서 시작한다.

일반 ELF addrenv 경로에서도 원리는 같다. `os/binfmt/libelf/libelf_addrenv.c`가 app RAM partition을 잡고, data/bss 뒤를 `BIN_HEAP`으로 계산하며, `sizeof(struct mm_heap_s)`를 heap usable size에서 제외한다.

### exec_module이 app heap을 초기화한다

`os/binfmt/binfmt_execmodule.c`의 `exec_module()`은 load가 끝난 `struct binary_s`를 실행 가능한 task로 만든다. app heap 초기화는 task 생성보다 먼저 수행된다.

흐름은 다음과 같다.

1. `binp->uheap = (struct mm_heap_s *)binp->sections[BIN_HEAP]`
2. `mm_initialize(binp->uheap, sections[BIN_HEAP] + sizeof(struct mm_heap_s), sizes[BIN_HEAP])`
3. `mm_add_app_heap_list(binp->uheap, binp->bin_name)`
4. app data section 시작 부분에 app heap pointer를 기록한다.
5. 현재 TCB의 `uheap`에도 app heap pointer를 기록한다.
6. 이후 `kmm_zalloc()`으로 새 task TCB를 만들고 app task stack을 생성한다.

이 순서 때문에 app code가 실행되기 전부터 app heap은 ready 상태이다. app에서 `malloc()`을 호출하면 common/user side의 `BASE_HEAP`이 app heap table을 통해 이 heap을 가리킨다.

### common binary와 heap table

`CONFIG_SUPPORT_COMMON_BINARY=y`이면 common binary는 app heap pointer table을 가진다. `os/binfmt/binfmt_loadbinary.c`는 app binary를 load할 때 common binary data section 안의 heap table에 `sections[BIN_HEAP]` 값을 넣는다.

`os/mm/umm_heap/umm_initialize.c`에는 다음 두 전역이 user/common binary용 section에 배치된다.

- `g_cur_app`: 현재 실행 중인 app id이다. app이 여러 개일 때 current app heap table index로 사용한다.
- `g_app_heap_table[CONFIG_NUM_APPS + 1]`: app id별 heap pointer table이다.

현재 defconfig는 `CONFIG_NUM_APPS=1`이므로 user side `BASE_HEAP`은 항상 `g_app_heap_table[1]`이다. app이 여러 개가 되면 `g_cur_app` 업데이트와 context switch 연동이 중요해진다.

### app1 RAM 예산과 실제 heap

`CONFIG_APP1_BIN_DYN_RAMSIZE=5242880`은 app1이 사용할 동적 RAM budget이다. 그러나 이 값 전체가 `malloc()` 가능한 heap이 되지는 않는다.

XIP ELF 기준으로 대략 다음처럼 나뉜다.

```
app RAM budget
|
+-- .data
+-- .bss
+-- struct mm_heap_s
+-- allocator usable heap
```

따라서 app heap 부족을 분석할 때는 defconfig의 dynamic RAM size만 보지 말고, 실제 ELF의 data/bss 크기와 `binfmt`가 출력하는 `BIN_HEAP` start/size 로그를 같이 확인해야 한다.

<a id="debug-and-failure-handling"></a>
## 디버그 및 실패 처리

이 defconfig는 MM debug 옵션이 많이 켜져 있다.

- `CONFIG_DEBUG_MM=y`
- `CONFIG_DEBUG_MM_ERROR=y`
- `CONFIG_DEBUG_MM_HEAPINFO=y`
- `CONFIG_DEBUG_MM_FREEINFO=y`
- `CONFIG_MMINFO=y`
- `CONFIG_ARCH_HAVE_HEAPCHECK=y`

`CONFIG_DEBUG_MM_HEAPINFO`가 켜지면 allocation node에 caller address, pid, memory state가 들어간다. 이 정보는 heap owner, peak/current allocation, leak 추적에 사용된다.

`CONFIG_DEBUG_MM_FREEINFO`가 켜지면 free node에 free caller address와 free pid가 들어간다. double-free가 발생하면 처음 free한 위치와 두 번째 free 시도 위치를 비교하는 데 도움이 된다.

allocation failure 흐름은 다음 순서로 본다.

1. `mm_malloc()`이 대상 heap에서 chunk를 찾지 못한다.
2. `sched_garbagecollection()`을 한 번 호출해 dead task stack 등 지연 정리 대상을 회수한다.
3. 같은 요청을 한 번 재시도한다.
4. 그래도 실패하면 wrapper가 `mm_manage_alloc_fail()`을 호출한다.
5. `CONFIG_MM_ASSERT_ON_FAIL=y`이면 failure가 강하게 드러날 수 있다.

free 관련 문제는 다음 순서로 본다.

1. `free()` 또는 `kmm_free()` 호출 API가 pointer의 출처와 맞는지 확인한다.
2. `mm_get_heap(mem)`이 어떤 heap을 찾는지 확인한다.
3. `MM_ALLOC_BIT`가 이미 지워져 double-free warning이 나는지 확인한다.
4. `CONFIG_DEBUG_MM_FREEINFO` 로그에서 첫 free 위치와 두 번째 free 위치를 본다.
5. heap corruption이 의심되면 `os/mm/mm_heap/mm_check_heap_corruption.c`와 heapinfo dump 경로를 같이 본다.

<a id="how-to-modify-the-pipeline"></a>
## 파이프라인 수정 방법

### Kernel heap 크기나 위치를 바꿀 때

수정 대상은 주로 다음이다.

- `build/configs/rtl8730e/loadable_ext_ddr_st7785/defconfig`
  - `CONFIG_RAM_KREGIONx_START`
  - `CONFIG_RAM_KREGIONx_SIZE`
  - 필요 시 `CONFIG_KMM_REGIONS`
  - 필요 시 `CONFIG_KMM_NHEAPS`
- `build/configs/rtl8730e/scripts/rlx8730e_img2_ddr.ld`
  - DDR memory range와 section 배치
- `os/arch/arm/src/common/up_allocateheap.c`
  - board/arch가 heap start를 어떻게 조정하는지

원칙은 defconfig의 RAM region과 linker script의 memory map이 같은 물리/가상 범위를 말해야 한다는 것이다. 둘 중 하나만 바꾸면 build는 되더라도 runtime에서 heap이 section, stack, reserved memory와 겹칠 수 있다.

### Multi-region 또는 multi-heap을 추가할 때

현재는 `CONFIG_KMM_REGIONS=1`, `CONFIG_KMM_NHEAPS=1`이다. region이나 heap을 늘리려면 다음을 같이 맞춘다.

- `CONFIG_KMM_REGIONS`를 실제 region 수로 늘린다.
- `CONFIG_KMM_NHEAPS`를 allocator instance 수로 늘린다.
- `CONFIG_RAM_KREGIONx_START`와 `CONFIG_RAM_KREGIONx_SIZE` list의 element 수를 region 수와 맞춘다.
- 여러 heap에 region을 나눠 붙일 경우 `CONFIG_RAM_KREGIONx_HEAP_INDEX` 계열 설정도 같이 확인한다.
- `up_add_kregion()`이 첫 번째 region 이후의 region을 `mm_initialize()` 또는 `mm_addregion()`으로 추가한다.

region은 물리적으로 불연속이어도 된다. 같은 heap index에 여러 region을 붙이면 allocator 하나가 여러 region을 관리한다. 서로 다른 heap index에 붙이면 `kmm_malloc_at()` 또는 heap 우선순위 정책을 신경 써야 한다.

### app heap budget을 바꿀 때

loadable app heap 부족이면 먼저 다음 값을 본다.

- `CONFIG_APP1_BIN_DYN_RAMSIZE`
- `CONFIG_APP1_MAIN_STACKSIZE`
- `CONFIG_COMMON_BIN_STATIC_RAMSIZE`
- `CONFIG_FLASH_PART_SIZE`
- `CONFIG_FLASH_PART_NAME`
- `CONFIG_FLASH_PART_TYPE`

`CONFIG_APP1_BIN_DYN_RAMSIZE`를 늘리면 app RAM partition이 커진다. 하지만 실제 `malloc()` 가능 heap은 app의 `.data`, `.bss`, `struct mm_heap_s`, stack/loader 요구사항을 뺀 나머지다. map 파일과 `binfmt` 로그에서 실제 `BIN_HEAP` size를 확인해야 한다.

app을 추가한다면 `CONFIG_NUM_APPS`, `CONFIG_APPn_*`, common binary의 heap table, binary manager table, flash partition table을 함께 늘려야 한다. `CONFIG_NUM_APPS > 1`이 되면 user side `BASE_HEAP`이 `g_app_heap_table[g_cur_app]`를 보므로 context switch 시 `g_cur_app`가 정확히 갱신되는지도 확인해야 한다.

### debug overhead를 줄일 때

현재 heap node는 debug 옵션 때문에 더 커진다.

- `CONFIG_DEBUG_MM_HEAPINFO`는 allocation owner 추적 정보를 node에 추가한다.
- `CONFIG_DEBUG_MM_FREEINFO`는 free owner 추적 정보를 free node에 추가한다.

문제 재현이 끝났거나 release footprint를 줄여야 한다면 이 옵션을 끄는 것을 검토할 수 있다. 단, node 크기와 alignment가 달라지므로 heap 사용량과 fragmentation 양상이 바뀔 수 있다.

<a id="common-pitfalls"></a>
## 일반적인 함정

- `CONFIG_RAM_SIZE`만 보고 kernel heap usable size라고 판단하지 않는다. `up_allocate_kheap()` 조정, linker section, stack, app RAM partition을 같이 봐야 한다.
- `CONFIG_APP1_BIN_DYN_RAMSIZE` 전체가 `malloc()` heap이 아니다. data/bss와 `struct mm_heap_s`를 제외한 나머지가 allocator usable heap이다.
- `kmm_malloc()` pointer를 `free()`로 해제하지 않는다. 반대도 마찬가지이다.
- `mm_get_heap()`이 `NULL`을 반환하는 pointer는 heap 밖 주소이거나 unload/reload 중 비활성 app heap일 수 있다.
- multi-region을 추가할 때 start/size list 개수와 `CONFIG_KMM_REGIONS`를 맞추지 않으면 region 일부가 초기화되지 않는다.
- linker script와 defconfig RAM range를 따로 수정하지 않는다. 둘은 항상 같은 memory map을 표현해야 한다.
- debug heapinfo/freeinfo가 켜진 상태의 node 크기를 release 설정의 node 크기와 동일하다고 가정하지 않는다.
- app reload/unload 경로에서는 app heap queue의 active 상태가 중요하다. 비활성 heap 주소를 free하려고 하면 `mm_get_heap()`이 실패할 수 있다.
