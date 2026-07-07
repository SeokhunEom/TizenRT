# Binfmt and Binary Manager Guide

이 문서는 TizenRT의 `binfmt`와 `binary_manager`가 loadable binary를 어떻게 찾고, 검증하고, 메모리에 올리고, task로 실행하고, 업데이트 또는 복구하는지 설명한다.

`binfmt`는 binary format loader이다. ELF/XIP ELF 같은 포맷별 handler를 등록하고, binary file을 읽어 `struct binary_s`에 text/data/bss/heap/entry 정보를 채운 뒤 `exec_module()`로 task를 만든다.

`binary_manager`는 loadable binary lifecycle manager이다. flash partition, bootparam, binary header, state, update/recovery 정책을 관리하고, 실제 load/exec 작업은 `binfmt`의 `load_binary()`로 넘긴다.

## Contents

- [Configuration Baseline](#configuration-baseline)
- [Layering](#layering)
- [Source Map](#source-map)
- [Data Structures](#data-structures)
- [Binfmt Flow](#binfmt-flow)
- [Binary Manager Boot Flow](#binary-manager-boot-flow)
- [Load and Execute Flow](#load-and-execute-flow)
- [State Machine](#state-machine)
- [Bootparam and A/B Partitions](#bootparam-and-ab-partitions)
- [Update Flow](#update-flow)
- [Recovery Flow](#recovery-flow)
- [Common Binary Handling](#common-binary-handling)
- [Debugging and Modification Points](#debugging-and-modification-points)
- [Common Pitfalls](#common-pitfalls)

## Configuration Baseline

현재 `rtl8730e/loadable_ext_ddr_st7785` 기준으로 관련 설정은 다음과 같다.

| 설정 | 값 | 의미 |
| --- | --- | --- |
| `CONFIG_BUILD_PROTECTED` | `y` | kernel/user 영역을 분리한다. |
| `CONFIG_APP_BINARY_SEPARATION` | `y` | user app을 kernel과 별도 binary로 만든다. |
| `CONFIG_ARCH_USE_MMU` | `y` | app address/memory protection 초기화 경로를 사용한다. |
| `CONFIG_FLASH_PARTITION` | `y` | partition table 기반으로 kernel/common/app/bootparam 영역을 관리한다. |
| `CONFIG_FLASH_PART_TYPE` | `none,none,none,none,kernel,bin,bin,kernel,bin,bin,bootparam,` | kernel/common/app1이 A/B set으로 배치된다. |
| `CONFIG_FLASH_PART_NAME` | `bl1,reserved,ftl,ss,kernel,common,app1,kernel,common,app1,bootparam,` | `common`과 `app1` partition 이름이 binary table key가 된다. |
| `CONFIG_NUM_APPS` | `1` | user app은 `app1` 하나이다. common binary는 index 0으로 별도 관리된다. |
| `CONFIG_SUPPORT_COMMON_BINARY` | `y` | common binary를 먼저 load하고 app이 common symbol/heap table을 참조한다. |
| `CONFIG_COMMON_BINARY_NAME` | `common` | common binary 이름이다. |
| `CONFIG_COMMON_BIN_STATIC_RAMSIZE` | `524288` | common binary static RAM budget이다. |
| `CONFIG_APP1_BIN_NAME` | `app1` | app binary 이름이다. |
| `CONFIG_APP1_BIN_TYPE` | `ELF` | app binary header의 type은 ELF여야 한다. |
| `CONFIG_APP1_BIN_DYN_RAMSIZE` | `5242880` | app1 RAM partition budget이다. |
| `CONFIG_APP1_MAIN_STACKSIZE` | `8192` | app1 main task stack size이다. |
| `CONFIG_APP1_MAIN_PRIORITY` | `180` | app1 main task priority이다. |
| `CONFIG_APP1_BIN_LOADING_PRIORITY` | `LOW` | initial load에서 loader thread priority를 낮게 잡는다. |
| `CONFIG_BINFMT_ENABLE` | `y` | binfmt subsystem을 켠다. |
| `CONFIG_BINFMT_LOADABLE` | `y` | loadable binary unload/exit 경로를 사용한다. |
| `CONFIG_XIP_ELF` | `y` | XIP ELF handler를 등록한다. text는 flash/XIP 주소를 사용하고 RAM에는 data/bss/heap을 둔다. |
| `CONFIG_BINFMT_CONSTRUCTORS` | `y` | C++ constructor hook을 지원한다. |
| `CONFIG_SAVE_BIN_SECTION_ADDR` | `y` | binary section 주소를 저장해 debug에 사용한다. |
| `CONFIG_COMPRESSED_BINARY` | `y` | binary manager log에서 compressed binary로 표시된다. |
| `CONFIG_USE_BP` | `y` | bootparam을 사용해 active kernel/app/resource set을 정한다. |
| `CONFIG_BINARY_MANAGER` | `y` | binary manager kernel thread를 실행한다. |
| `CONFIG_BINMGR_UPDATE` | `y` | update 요청과 bootparam 갱신을 지원한다. |
| `CONFIG_BINMGR_UPDATE_SAME_VERSION` | `y` | 같은 version binary도 update 대상으로 허용한다. |
| `CONFIG_BINMGR_RELOAD_REBOOT` | `y` | update/reload 시 process reload보다 board reboot 정책을 사용한다. |
| `CONFIG_RESOURCE_FS` | `y` | resource binary/partition도 binary manager 관리 대상에 포함될 수 있다. |

이 조합에서는 bootparam이 active set을 정하고, binary manager가 `common`을 먼저 load한 뒤 `app1`을 load한다. app update는 inactive partition에 새 binary가 있으면 bootparam을 갱신하거나 board reset으로 새 set을 사용하게 한다.

## Layering

두 모듈의 책임은 분명히 나뉜다.

```text
binary_manager
  - partition registration
  - bootparam scan/write/recovery
  - binary header/signature/CRC validation
  - active/inactive partition selection
  - binary state table
  - update/recovery policy
  - loader thread scheduling
        |
        v
binfmt
  - binary format handler registry
  - ELF/XIP ELF format load
  - section address/size population
  - app heap initialization
  - task creation and activation
  - module unload resource cleanup
```

따라서 문제를 볼 때도 먼저 어떤 계층 문제인지 분리한다.

- partition, header, bootparam, update path 문제이면 `binary_manager`를 본다.
- ELF parsing, userspace metadata, section address, entry point, app heap, task activation 문제이면 `binfmt`를 본다.
- app이 시작된 뒤 `BINARY_RUNNING`으로 바뀌지 않으면 app의 `binary_manager_notify_binary_started()` 호출과 binary manager request path를 같이 본다.

## Source Map

| 영역 | 파일 | 역할 |
| --- | --- | --- |
| binfmt public API | `os/include/tinyara/binfmt/binfmt.h` | `struct binary_s`, `struct binfmt_s`, `BIN_TEXT`/`BIN_HEAP` section enum |
| binfmt init | `os/binfmt/binfmt_initialize.c` | enabled format handler를 등록한다. |
| format registry | `os/binfmt/binfmt_register.c` | `g_binfmts` handler list에 loader를 추가한다. |
| generic loader | `os/binfmt/binfmt_loadmodule.c` | registered handler를 순회하며 format load를 시도한다. |
| binary manager bridge | `os/binfmt/binfmt_loadbinary.c` | `load_binary()`로 `load_module()` + `exec_module()`을 묶는다. |
| execution | `os/binfmt/binfmt_execmodule.c` | app heap 초기화, TCB/stack 생성, task activation |
| unload | `os/binfmt/binfmt_unloadmodule.c` | format-specific unload와 section memory cleanup |
| task exit cleanup | `os/binfmt/binfmt_exit.c` | app task exit 시 unload, delayed free cleanup, app heap list cleanup |
| XIP ELF loader | `os/binfmt/libxipelf/xipelf.c` | `userspace_s` metadata를 읽어 XIP ELF section 정보를 채운다. |
| binary manager public API | `os/include/tinyara/binary_manager.h` | request/result enum, binary header, public registration API |
| manager internals | `os/kernel/binary_manager/binary_manager_internal.h` | thread priority, binary table, bootparam structure, macro accessors |
| manager thread | `os/kernel/binary_manager/binary_manager.c` | message queue loop와 request dispatch |
| partition/state data | `os/kernel/binary_manager/binary_manager_data.c` | kernel/user binary table, partition registration, version scan |
| loader policy | `os/kernel/binary_manager/binary_manager_load.c` | loader thread, load all/update/reload, terminate binary |
| bootparam | `os/kernel/binary_manager/binary_manager_bootparam.c` | bootparam scan, validation, write, set alignment/recovery |
| header/CRC verify | `os/kernel/binary_manager/binary_manager_verify.c` | binary header validation과 CRC check |
| response path | `os/kernel/binary_manager/binary_manager_response.c` | requester response message queue send |
| callback path | `os/kernel/binary_manager/binary_manager_callback.c` | app state callback registration/notification |
| fault recovery | `os/kernel/binary_manager/binary_manager_recovery.c` | fault 발생 시 task deactivation과 reload/reboot decision |
| partition discovery | `os/board/common/partitions.c` | flash partition을 binary manager에 등록한다. |
| OS init | `os/kernel/init/os_start.c` | `binfmt_initialize()` 호출 |
| bringup | `os/kernel/init/os_bringup.c` | `binary_manager` kernel thread 생성 |

## Data Structures

### `struct binary_s`

`struct binary_s`는 binfmt와 binary manager 사이를 지나가는 runtime load descriptor이다.

주요 필드는 다음과 같다.

- `filename`: load할 binary path이다. binary manager는 보통 `/dev/mtdblockN` 형태를 전달한다.
- `uheap`: app heap object pointer이다. `exec_module()`에서 `sections[BIN_HEAP]`를 `struct mm_heap_s *`로 해석한다.
- `ramstart`, `ramsize`: app RAM partition 시작과 크기이다.
- `islibrary`: common binary이면 true이다.
- `entrypt`: app entry point이다.
- `sections[BIN_MAX]`: text/ctor/dtor/ro/data/bss/heap 시작 주소이다.
- `sizes[BIN_MAX]`: 각 section 크기이다.
- `flash_region_start`, `flash_region_end`: XIP ELF text가 위치한 flash region이다.
- `ram_region_start`, `ram_region_end`: XIP ELF가 사용하는 RAM region이다.
- `ctors`, `dtors`, `nctors`, `ndtors`: constructor/destructor 정보이다.
- `priority`, `stacksize`: app main task 생성에 사용한다.
- `filelen`, `offset`: MTD partition 안에서 binary payload를 읽을 때 사용한다.
- `binary_idx`, `bin_ver`, `bin_name`: binary manager table과 연결하는 key이다.
- `unload`: format handler가 제공하는 unload callback이다.

`sections[]` enum은 다음 순서이다.

```text
BIN_TEXT
BIN_CTOR
BIN_DTOR
BIN_RO
BIN_DATA
BIN_BSS
BIN_HEAP
BIN_MAX
```

`BIN_HEAP`은 app heap 전체의 기준 주소이다. XIP ELF 기준으로 `sections[BIN_HEAP]`에는 `struct mm_heap_s`가 놓이고, 실제 allocator usable heap은 그 뒤에서 시작한다.

### `struct binfmt_s`

`struct binfmt_s`는 format handler이다.

```text
next
load(struct binary_s *bin)
unload(struct binary_s *bin)
```

`binfmt_initialize()`가 enabled format의 initialize 함수를 호출하고, 각 format initialize 함수가 `register_binfmt()`로 handler를 `g_binfmts` list에 등록한다. 현재 기준 설정은 `CONFIG_XIP_ELF=y`이므로 `xipelf_initialize()`가 XIP ELF handler를 등록한다.

### `binmgr_uinfo_t`

`binmgr_uinfo_t`는 binary manager의 user/common binary table row이다.

주요 필드는 다음과 같다.

- `bin_id`: app main task pid이다.
- `state`: `BINARY_INACTIVE`, `BINARY_LOADED`, `BINARY_RUNNING` 등 runtime state이다.
- `useidx`: 현재 active partition index이다. A/B partition이므로 보통 0 또는 1이다.
- `bp_idx`: bootparam 안의 app_data index이다.
- `bin_count`: 이 binary 이름으로 등록된 partition 수이다.
- `load_attr`: load 시 사용할 name, size, RAM size, stack, priority, offset, version이다.
- `part_info[PARTS_PER_BIN]`: partition size, mtd block number, physical address이다.
- `load_priority[PARTS_PER_BIN]`: partition별 loading priority이다.
- `bin_ver[PARTS_PER_BIN]`: partition별 binary version이다.
- `rt_list`, `nrt_list`: 해당 binary가 만든 real-time/non-real-time thread list이다.
- `cb_list`: state callback list이다.
- `binp`: load된 `struct binary_s` pointer이다.

`CONFIG_SUPPORT_COMMON_BINARY=y`이면 user binary table의 index 0은 common binary이다. 실제 app은 index 1부터 시작한다.

### Binary headers

binary manager는 partition에서 header를 먼저 읽고 검증한다.

- `kernel_binary_header_t`: kernel version, binary size, secure header size를 가진다.
- `common_binary_header_t`: common version과 binary size를 가진다.
- `user_binary_header_t`: app name, type, loading priority, binary size, RAM size, stack size, app priority, kernel version을 가진다.
- `resource_binary_header_t`: resource version과 size를 가진다.

user binary header에서 `bin_type`은 `BIN_TYPE_ELF`여야 한다. `bin_ramsize`, `bin_size`, `loading_priority`, `bin_ver`가 0이면 invalid header로 처리된다.

## Binfmt Flow

### 1. initialization

`os/kernel/init/os_start.c`가 boot 중 `binfmt_initialize()`를 호출한다. `binfmt_initialize()`는 설정에 따라 format handler를 등록한다.

현재 기준에서는 다음 경로가 중요하다.

```text
os_start()
  -> binfmt_initialize()
    -> xipelf_initialize()
      -> register_binfmt(&g_xipelfbinfmt)
```

`register_binfmt()`는 scheduler lock을 잡고 handler를 `g_binfmts` list head에 붙인다.

### 2. generic load

`load_module(struct binary_s *bin)`은 format 독립 entry이다.

흐름은 다음과 같다.

1. `bin->priority`가 0이면 현재 thread priority를 default priority로 복사한다.
2. `CONFIG_LIB_ENVPATH`가 켜져 있고 path가 relative이면 PATH 후보를 순회한다.
3. absolute path를 기준으로 `load_absmodule()`을 호출한다.
4. `load_absmodule()`은 `g_binfmts` list를 순회하면서 각 handler의 `load(bin)`을 호출한다.
5. handler가 `OK`를 반환하면 `bin->unload`에 handler unload callback을 저장한다.

현재 XIP ELF에서는 `g_xipelfbinfmt.load = xipelf_loadbinary`이고 unload callback은 `NULL`이다.

### 3. XIP ELF load

`xipelf_loadbinary()`는 전체 ELF를 RAM으로 복사하는 loader가 아니다. XIP ELF binary 안의 `struct userspace_s` metadata를 읽어 text/data/bss/heap 주소를 설정한다.

흐름은 다음과 같다.

1. binary header와 checksum 영역을 건너뛰기 위해 offset을 계산한다.
2. common binary이면 `common_binary_header_t`, app이면 `user_binary_header_t` 크기를 반영한다.
3. signing이 켜져 있으면 signing prepend size도 반영한다.
4. MTD block 또는 `up_read_decrypted_flash()`로 `struct userspace_s`를 읽는다.
5. `uspace.text_start`, `flash_end`로 `BIN_TEXT` 주소와 크기를 채운다.
6. `.bss` RAM 영역을 0으로 초기화한다.
7. flash의 data initializer를 RAM data 영역으로 copy한다.
8. `BIN_DATA`, `BIN_BSS`, `BIN_HEAP` 주소와 크기를 채운다.
9. `entrypt`, constructor list, exception index 정보를 채운다.

XIP ELF에서 heap size 계산은 다음이다.

```text
sections[BIN_HEAP] = uspace.heap_start
sizes[BIN_HEAP]    = uspace.heap_end - uspace.heap_start - sizeof(struct mm_heap_s)
```

즉 app heap object 자체는 `sections[BIN_HEAP]`에 놓이고, allocator가 사용할 usable heap은 `sections[BIN_HEAP] + sizeof(struct mm_heap_s)`부터이다.

### 4. exec

`exec_module()`은 load된 `struct binary_s`를 실제 task로 만든다.

흐름은 다음과 같다.

1. `entrypt`와 `stacksize`가 유효한지 확인한다.
2. `binp->uheap = (struct mm_heap_s *)binp->sections[BIN_HEAP]`
3. `mm_initialize(binp->uheap, sections[BIN_HEAP] + sizeof(struct mm_heap_s), sizes[BIN_HEAP])`
4. `mm_add_app_heap_list(binp->uheap, binp->bin_name)`
5. app data section 첫 4 bytes에 app heap pointer를 기록한다.
6. 현재 loader task TCB의 `uheap`을 임시로 app heap으로 설정한다.
7. `kmm_zalloc()`으로 새 task TCB를 만든다.
8. `up_create_stack()`으로 app stack을 만든다.
9. `task_init()`으로 app main task를 초기화한다.
10. constructor hook이 켜져 있으면 `task_starthook()`으로 constructor 실행 hook을 등록한다.
11. loader TCB의 임시 `uheap`, MPU/MMU 관련 값을 원복한다.
12. 새 task의 `uspace`, `uheap`, `app_id`, `tg_binidx`, name을 채운다.
13. `binary_manager_add_binlist()`로 binary thread list에 등록한다.
14. binary table을 `BINARY_LOADED`로 갱신한다.
15. `newtcb->bininfo = binp`로 exit 시 cleanup 정보를 연결한다.
16. `task_activate()`로 app task를 ready/running 상태로 보낸다.

`BINARY_LOADED`는 task가 생성되어 scheduler에 올라갔다는 뜻이지, app main이 초기화 완료를 알렸다는 뜻은 아니다. app이 시작 후 `binary_manager_notify_binary_started()`를 호출하면 binary manager가 `BINARY_RUNNING`으로 전환한다.

### 5. unload and exit

loadable task가 종료되면 `binfmt_exit()`가 module resource를 정리한다.

흐름은 다음과 같다.

1. `unload_module(bin)`을 호출한다.
2. format-specific unload callback이 있으면 실행한다.
3. destructor가 켜져 있으면 destructor를 실행한다.
4. argv buffer를 해제한다.
5. XIP ELF가 아닌 경우 section memory를 해제한다.
6. architecture memory protection을 deinit한다.
7. section address debug 정보를 지운다.
8. delayed `kufree` list에서 app heap 범위 안의 주소를 제거한다.
9. app heap list를 disable/remove한다.
10. app RAM partition과 `struct binary_s`를 `kmm_free()`한다.

`CONFIG_OPTIMIZE_APP_RELOAD_TIME`이 켜진 ELF reload 경로에서는 `bin->reload` flag에 따라 section memory를 유지할 수 있다. 현재 기준은 `CONFIG_XIP_ELF=y`이고 `CONFIG_OPTIMIZE_APP_RELOAD_TIME`은 ELF 의존 옵션이라 핵심 경로가 아니다.

## Binary Manager Boot Flow

binary manager thread는 `os/kernel/init/os_bringup.c`에서 생성된다.

```text
kernel_thread(
  BINARY_MANAGER_NAME,
  BINARY_MANAGER_PRIORITY,
  BINARY_MANAGER_STACKSIZE,
  binary_manager,
  NULL
)
```

thread 시작 후 `binary_manager()`는 다음 순서로 준비한다.

1. `CONFIG_USE_BP=y`이면 `binary_manager_update_bpinfo()`로 bootparam을 scan한다.
2. bootparam dump를 출력한다.
3. registered kernel partition이 있는지 확인하고 `binary_manager_scan_kbin()`으로 running kernel 정보를 채운다.
4. `CONFIG_RESOURCE_FS=y`이면 resource를 mount한다.
5. `CONFIG_APP_BINARY_SEPARATION=y`이면 registered user binary partition이 있는지 확인한다.
6. signal mask를 설정한다.
7. recovery thread가 켜져 있으면 fault message sender thread를 만든다.
8. `BINMGR_REQUEST_MQ` message queue를 만든다.
9. app binary separation이면 `binary_manager_execute_loader(LOADCMD_LOAD_ALL, 0)`으로 initial load를 시작한다.
10. 이후 무한 loop에서 request message를 받고 command별 handler로 dispatch한다.

partition 등록은 `os/board/common/partitions.c`가 flash partition을 순회하면서 수행한다.

- partition type `kernel` -> `binary_manager_register_kpart()`
- partition type `bin` + name `common`/`app1` -> `binary_manager_register_upart()`
- partition type `bootparam` -> `binary_manager_register_bppart()`
- resource partition -> `binary_manager_register_respart()`

현재 defconfig의 partition 이름은 `kernel,common,app1,kernel,common,app1,bootparam` 패턴이므로 kernel/common/app1 모두 A/B partition을 가진다.

## Load and Execute Flow

initial load는 `LOADCMD_LOAD_ALL`에서 시작한다.

```text
binary_manager()
  -> binary_manager_execute_loader(LOADCMD_LOAD_ALL, 0)
    -> kernel_thread("bm_loader", LOADER_PRIORITY_HIGH, ..., loadingall_thread)
      -> binary_manager_scan_ubin_all()
      -> binary_manager_load(BM_CMNLIB_IDX)
      -> binary_manager_load(apps with HIGH loading priority)
      -> binary_manager_execute_loader(LOADCMD_LOAD, app_idx) for lower priority apps
```

`binary_manager_load()`는 binary 하나를 실제로 load한다.

1. binary table state가 `BINARY_INACTIVE`인지 확인한다.
2. active partition path를 `/dev/mtdblockN` 형태로 만든다.
3. signing이 켜져 있으면 signature를 검증한다.
4. common이면 `common_binary_header_t`, app이면 `user_binary_header_t`를 읽는다.
5. `binary_manager_read_header()`가 header field와 CRC를 검증한다.
6. header에서 `load_attr_t`를 만든다.
7. `binary_manager_load_binary()`를 호출한다.
8. `binary_manager_load_binary()`가 retry loop 안에서 `load_binary(bin_idx, path, &load_attr)`를 호출한다.
9. 성공하면 `BIN_LOAD_ATTR`, `BIN_NAME`, `BIN_LOADVER`, section debug 정보를 table에 반영한다.

`load_binary()`는 binary manager와 binfmt 사이의 bridge이다.

```text
binary_manager_load_binary()
  -> load_binary()
    -> allocate and fill struct binary_s
    -> load_module()
      -> xipelf_loadbinary()
    -> elf_save_bin_section_addr()
    -> binfmt_arch_init_mem_protect()
    -> common binary이면 table 갱신 후 return
    -> app binary이면 common heap table 갱신
    -> exec_module()
      -> app heap initialize
      -> task_init()
      -> task_activate()
```

common binary는 app task를 만들지 않는다. `load_binary()`에서 `bin->islibrary`이면 `BIN_STATE(BM_CMNLIB_IDX) = BINARY_RUNNING`으로 갱신하고 return한다.

user app은 `exec_module()`에서 `BINARY_LOADED`가 된다. app main이 `binary_manager_notify_binary_started()`를 호출하면 manager request `BINMGR_NOTIFY_STARTED`를 통해 `binary_manager_update_running_state()`가 호출되고 `BINARY_RUNNING`이 된다.

## State Machine

user binary state는 `enum binary_state`로 정의된다.

| State | 의미 | 주로 바뀌는 위치 |
| --- | --- | --- |
| `BINARY_UNREGISTERED` | file/partition이 등록되지 않음 | 초기/오류 상태 |
| `BINARY_INACTIVE` | partition은 있으나 아직 load되지 않음 | `binary_manager_register_upart()`, unload 완료 |
| `BINARY_LOADED` | task가 생성/activate됨 | `exec_module()` |
| `BINARY_RUNNING` | app이 started notify를 보냄 | `binary_manager_update_running_state()` |
| `BINARY_UNLOADING` | unload 중복 방지를 위해 unload 진행 중 | `binary_manager_terminate_binary()` |
| `BINARY_FAULT` | fault recovery 대상으로 표시됨 | `binary_manager_deactivate_binary()` |

일반 initial boot에서는 다음 흐름이 된다.

```text
partition registered
  -> BINARY_INACTIVE
  -> binary_manager_load()
  -> exec_module()
  -> BINARY_LOADED
  -> app calls binary_manager_notify_binary_started()
  -> BINARY_RUNNING
```

unload/update/recovery에서는 다음 흐름이 추가된다.

```text
BINARY_RUNNING
  -> BINARY_UNLOADING
  -> task_terminate_unloaded()
  -> binfmt_exit()
  -> BINARY_INACTIVE
  -> binary_manager_load()
```

fault path에서는 다음처럼 들어간다.

```text
fault detected
  -> BINARY_FAULT
  -> LOADCMD_RELOAD or board reset policy
```

현재 기준 설정은 `CONFIG_BINMGR_RELOAD_REBOOT=y`이므로 recovery/update에서 in-place reload보다 reset 정책이 우선한다.

## Bootparam and A/B Partitions

`CONFIG_USE_BP=y`이면 bootparam이 active binary set을 결정한다. bootparam partition은 8 KiB이고, 내부적으로 4 KiB slot 두 개를 가진다.

```text
bootparam partition
|
+-- BP slot 0, 4 KiB
+-- BP slot 1, 4 KiB
```

`binmgr_bpdata_t`는 head와 tail로 구성된다.

head 주요 필드는 다음이다.

- `crc_hash`: bootparam CRC이다.
- `version`: bootparam version이다. 더 큰 version이 최신이다.
- `format_ver`: bootparam format version이다.
- `active_idx`: active kernel set이다. 보통 0이면 A, 1이면 B이다.
- `address[BOOTPARAM_COUNT]`: kernel partition address이다.
- `app_count`: app/common entry 수이다.
- `app_data[]`: app/common name과 `useidx`이다.
- `resource_active_idx`: resource active set이다.

tail 주요 필드는 다음이다.

- `bp_update_reason`: bootloader/binary manager/recovery가 왜 bootparam을 갱신했는지 기록한다.

`binary_manager_scan_bootparam()`은 두 slot을 모두 읽고, CRC와 field를 검증한 뒤 가장 version이 큰 valid slot을 `g_bp_info.inuse_idx`로 선택한다.

`binary_manager_write_bootparam()`은 현재 in-use slot이 아니라 반대 slot에 쓴다.

```text
write target = g_bp_info.inuse_idx ^ 1
```

즉 bootparam update는 active slot을 덮어쓰지 않고 inactive BP slot에 새 version을 쓴다.

### Set alignment

set alignment는 kernel/common/app/resource의 A/B index가 서로 맞는지 보는 정책이다.

`binary_manager_is_set_mismatch()`는 bootparam의 kernel `active_idx`와 app/resource `useidx`가 모두 같은 set인지 검사한다. 정상 set은 모두 A이거나 모두 B이다.

불일치가 있으면 `binary_manager_recover_bootparam_set()`이 partition을 직접 검증하고, A/B 중 valid set의 가장 높은 version을 기준으로 새 bootparam을 만든 뒤 reboot한다.

## Update Flow

외부 요청은 message queue로 `BINMGR_UPDATE` 또는 `BINMGR_SETBP` 형태로 들어온다.

### `BINMGR_UPDATE`

`BINMGR_UPDATE`는 manager thread에서 다음처럼 처리된다.

```text
BINMGR_UPDATE
  -> binary_manager_execute_loader(LOADCMD_UPDATE, 0)
    -> update_thread()
      -> binary_manager_check_update()
      -> CONFIG_BINMGR_RELOAD_REBOOT=y이면 binary_manager_reset_board()
      -> 아니면 running app/common unload 후 LOADCMD_LOAD_ALL
```

`binary_manager_check_update()`는 kernel inactive partition을 먼저 본다. 새 kernel binary가 있으면 reboot path로 간다. app binary separation이 켜져 있으면 common/app inactive partition도 scan하고, update 가능하면 `BIN_USEIDX(bin_idx) ^= 1`로 active candidate를 반전한다.

현재 기준은 `CONFIG_BINMGR_RELOAD_REBOOT=y`이므로 update가 확인되면 board reset으로 새 binary set을 반영한다.

### `BINMGR_SETBP`

`BINMGR_SETBP`는 bootparam 자체를 갱신하는 request이다.

```text
BINMGR_SETBP
  -> binary_manager_update_bootparam()
    -> current BP copy
    -> version++
    -> requested binary group 검사
    -> update 가능한 항목은 useidx/active_idx 반전
    -> binary_manager_write_bootparam()
    -> requester response
```

group bit는 `BINARY_KERNEL`, `BINARY_COMMON`, `BINARY_USERAPP`, `BINARY_RESOURCE`를 대상으로 한다. 모든 요청 대상이 updatable이면 inactive BP slot에 새 bootparam을 쓴다. 일부가 already updated/not found이면 response에 해당 결과를 넣고 전체 결과를 조정한다.

## Recovery Flow

recovery는 설정에 따라 크게 두 가지 정책이 있다.

### In-place reload policy

`CONFIG_BINMGR_RECOVERY=y`이고 `CONFIG_BINMGR_RELOAD_REBOOT=n`이면 fault 발생 시 app task들을 deactivation하고 loader thread로 reload한다.

흐름은 다음과 같다.

```text
fault
  -> binary_manager_recover_userfault()
    -> binary_manager_deactivate_rtthreads()
    -> binary_manager_unblock_fault_message_sender()
      -> faultmsg sender sends BINMGR_FAULT
        -> binary_manager_recovery()
          -> binary_manager_deactivate_binary()
          -> binary_manager_execute_loader(LOADCMD_RELOAD, bin_idx)
            -> reloading_thread()
              -> binary_manager_terminate_binary()
              -> binary_manager_deinit_modules()
              -> binary_manager_execute_loader(LOADCMD_LOAD or LOADCMD_LOAD_ALL)
```

common binary가 켜져 있으면 user app 하나만 reload하지 않고 common과 모든 user app을 함께 unload/reload한다. common binary thread/resource가 user app thread list와 연결되어 있기 때문이다.

### Reboot reload policy

현재 기준은 `CONFIG_BINMGR_RELOAD_REBOOT=y`이다. 이 경우 `CONFIG_BINMGR_RECOVERY`는 의존성상 꺼지고, update/reload는 board reset으로 수렴한다.

이 정책에서는 fault나 update 시 in-place task cleanup 복잡도를 줄이는 대신 reboot 비용을 감수한다. 문서나 코드 수정 시 recovery path를 설명할 때 현재 defconfig가 reload thread를 실제로 쓰지 않는다는 점을 구분해야 한다.

## Common Binary Handling

`CONFIG_SUPPORT_COMMON_BINARY=y`일 때 common binary는 index 0으로 관리된다.

핵심 차이는 다음과 같다.

- common binary는 `BM_CMNLIB_IDX = 0`이다.
- common binary 이름은 `BM_CMNLIB_NAME = "common"`이다.
- common binary는 app task를 만들지 않는다.
- `load_binary()`는 common binary load 성공 시 `BINARY_RUNNING`으로 바로 둔다.
- app binary load 전에 common binary가 먼저 load되어야 한다.
- app binary는 link 시 common binary symbol을 참조하고, runtime에서는 common data section의 heap table을 통해 app heap pointer를 공유한다.

`load_binary()`에서 app binary를 load할 때 다음 일이 일어난다.

```text
heap_table = (uint32_t *)(g_lib_binp->sections[BIN_DATA] + 8)
heap_table[binary_idx] = bin->sections[BIN_HEAP]
```

즉 common binary의 data section 안에 app별 heap pointer table이 있고, user side allocator는 그 table을 통해 현재 app heap을 찾는다.

common binary constructor도 특별하다. `CONFIG_BINFMT_CONSTRUCTORS=y`이면 app task start hook에서 common binary constructor를 먼저 한 번 실행하고, 이후 app constructor를 실행한다.

## Debugging and Modification Points

### app이 load되지 않을 때

확인 순서는 다음이 좋다.

1. flash partition table에 `kernel`, `common`, `app1`, `bootparam`이 기대한 순서와 size로 있는지 본다.
2. `os/board/common/partitions.c`가 `binary_manager_register_upart()`를 호출했는지 본다.
3. `binary_manager_scan_ubin_all()`에서 active partition을 찾았는지 본다.
4. `binary_manager_read_header()`가 header field 또는 CRC에서 실패했는지 본다.
5. `binary_manager_load_binary()` retry log를 본다.
6. `load_binary()` 이후 `xipelf_loadbinary()`가 userspace metadata를 제대로 읽었는지 본다.
7. `exec_module()`에서 heap init, stack create, task init, task activate 중 어디서 실패했는지 본다.

### app이 `LOADED`에서 `RUNNING`으로 바뀌지 않을 때

`exec_module()`이 성공하면 state는 `BINARY_LOADED`가 된다. `BINARY_RUNNING`은 app이 binary manager에 started notify를 보낼 때 바뀐다.

확인할 것:

- app main에서 `binary_manager_notify_binary_started()`를 호출하는지
- app이 그 호출 전에 crash 또는 block되는지
- `BINMGR_NOTIFY_STARTED` request가 manager queue에 들어가는지
- `binary_manager_update_running_state()`가 pid의 `tg_binidx`를 찾는지

### binary header를 수정할 때

수정 범위는 다음을 함께 봐야 한다.

- build 후처리 도구가 header를 쓰는 방식
- `os/include/tinyara/binary_manager.h`의 packed header 구조체
- `binary_manager_verify_header_data()`의 field validation
- `binary_manager_read_header()`의 CRC 계산 범위
- XIP ELF loader의 header skip offset 계산

header 구조체 크기나 padding이 바뀌면 XIP ELF metadata offset도 같이 영향을 받는다.

### 새로운 binary type을 추가할 때

단순히 enum만 추가하면 안 된다. 최소한 다음을 같이 수정해야 한다.

- `enum binary_type_e`
- header 구조체
- `binary_manager_verify_header_data()`
- `binary_manager_read_header()`
- partition registration
- update/check path
- bootparam field가 필요한지 여부
- loader가 `struct binary_s`를 채울 수 있는지 여부

### loader priority를 바꿀 때

`binary_manager_get_loader_priority()`는 binary header의 `loading_priority`를 loader thread priority로 바꾼다.

- `BINARY_LOADPRIO_HIGH` -> `LOADER_PRIORITY_HIGH` 200
- `BINARY_LOADPRIO_MID` -> `LOADER_PRIORITY_MID` 150
- `BINARY_LOADPRIO_LOW` -> `LOADER_PRIORITY_LOW` 90

initial load에서 high priority app은 `loadingall_thread()` 안에서 직접 load하고, low/mid priority app은 별도 loader thread로 넘겨 낮은 priority에서 로드한다.

### bootparam 정책을 바꿀 때

다음 invariant를 유지해야 한다.

- bootparam slot 크기는 4 KiB이다.
- bootparam partition은 두 slot이므로 8 KiB여야 한다.
- `binmgr_bpdata_t`는 4 KiB 안에 들어가야 한다.
- 새 bootparam은 inactive slot에 써야 한다.
- CRC는 `CHECKSUM_SIZE` 이후부터 4 KiB 끝까지 계산한다.
- A/B set alignment를 깨면 recovery가 set을 다시 고르고 reboot할 수 있다.

## Common Pitfalls

- `binfmt`와 `binary_manager`를 같은 모듈로 보면 원인 추적이 느려진다. partition/header/state는 manager, section/entry/task 생성은 binfmt이다.
- `BINARY_LOADED`를 app 초기화 완료로 해석하지 않는다. app이 started notify를 보내야 `BINARY_RUNNING`이다.
- common binary는 task가 아니므로 app과 같은 `exec_module()` 완료 흐름을 기대하면 안 된다.
- `BIN_HEAP` size는 `struct mm_heap_s` 크기를 제외한 usable heap size이다. 하지만 `sections[BIN_HEAP]` 주소 자체에는 heap object가 놓인다.
- XIP ELF에서 text는 RAM copy가 아니라 XIP flash 주소이다. data/bss/heap만 RAM 관점에서 본다.
- bootparam update는 현재 slot을 덮어쓰지 않고 반대 slot에 쓴다.
- A/B partition을 추가할 때 `CONFIG_FLASH_PART_NAME`, `CONFIG_FLASH_PART_TYPE`, partition size, bootparam app data가 함께 맞아야 한다.
- `CONFIG_BINMGR_RELOAD_REBOOT=y`인 설정에서 in-place recovery thread 동작을 기대하지 않는다.
- binary header 구조체를 바꾸면 CRC, offset, post-build header 생성 도구, XIP ELF metadata offset을 함께 점검해야 한다.
