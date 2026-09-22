# 5.4.0-rc.268의 16바이트 MemLeak 분석

분석일: 2026-09-22. 입력은 `/Volumes/T7/Dev/ecode/conan/issue/`의 로그, ELF, map 파일이다. 원본과 제품 코드는 변경하지 않았다. 이 문서의 결론은 로그·ELF 분석 및 검사기 코드의 host 재현에 한정한다.

이 저장소에는 분석 문서만 이동했다. 증거 파일·고정 소스·재현 스크립트는 로컬 `/Volumes/T7/Dev/ecode/conan/issue/analysis-20260922/`에 보관되어 있으며, 아래 셸 명령은 `/Volumes/T7/Dev/ecode/conan`에서 실행한다. 소스 링크는 분석 당시의 고정 커밋을 가리키며 내부 GitHub 접근 권한이 필요하다.

## 1. 결론

`0x613e8c60`은 **app1 사용자 힙 검사에서 보고된 16바이트 블록**이다. 원본 Owner `0x0eb7d839`는 제공된 `app1_dbg`의 **`operator new(unsigned int)`** 안에 있으며, 할당 당시 기록된 PID 117은 검출 당시 **`product_scube_handlerReactor`**로 확인된다.

따라서 현재의 조사 출발점은 **제품 SCube 처리 문맥에서 호출한 C++ 할당 경로**다. 자동 보고서의 `kernel / Command handler slave / leak_addr`를 실제 원인 패키지·당시 스레드·할당 소스 위치로 사용하는 것은 부정확하다. 다만 사용자 힙에서 검출됐다는 사실만으로 allocator/표준 라이브러리/검사기 결함까지 배제할 수는 없다.

**어떤 상위 `new` 문장에서 발생했으며 왜 해제되지 않았는지는 아직 확정할 수 없다.** 로그에는 `operator new()` 위쪽 호출 스택, 원래 요청 크기, 전체 힙 및 해당 블록 헤더가 없다. 추가로 검사기가 이미 해제된 블록을 LEAK로 출력할 수 있는 결함을 host에서 재현했으므로, 현재 블록의 할당 상태도 먼저 확인해야 한다.

## 2. 원본 로그로 바로잡은 사실

| 항목 | 확인 결과 | 근거 |
|---|---|---|
| 검사 대상 힙 | `app1` | 원본 로그 23595–23604행: Kernel은 NO MEMORY LEAK, 이후 app1 검사 |
| 블록 주소 | `0x613e8c60` | 23627행 |
| 표시 크기 | 16 bytes | 요청 크기가 아니라 `node->size - SIZEOF_MM_ALLOCNODE` |
| 실제 Owner | `0x0eb7d839` | 23627행; 블록 주소와 다름 |
| 검출 당시 PID 117 | `product_scube_handlerReactor` | 23533, 61826, 99423, 131849, 168969행 |
| 관측 반복 | 같은 주소·Owner·PID·DATA가 5회 | 23627, 61921, 99515, 131941, 169059행 |
| 관측 구간 | 13:24:04.355–14:04:23.333 | 약 40분간 동일 블록이 보고됨 |
| 이후 이벤트 | 14:08:40.624 scheduler assertion, 14:08:44 reboot | 186348, 187200, 187256행 |
| 재부팅 후 PID 117 | `Command handler slave` | 200509행; 이전 부팅의 PID와 같은 태스크로 취급하면 안 됨 |
| 재부팅 후 검사 | 대상 주소는 없음 | 200581–200602행; 재부팅은 수정 효과의 증거가 아님 |

원본 로그의 전체 app1 검사 결과는 차례로 **9, 10, 9, 10, 9, 6개**다. 이번 issue 항목은 그중 하나다. 이번 파일에서 대상 블록 5회 출력은 고유 주소 1개의 반복 관측이며, 새 누수가 5번 생겼다는 증거는 아니다. 최초 할당 시점도 알 수 없다.

사용자 개요의 48바이트는 rc.259/260/268의 세 항목을 합한 값이다. 이전 버전의 원본 로그·ELF는 제공된 폴더에 없으므로, 이전 두 주소의 Owner나 동일 원인 여부는 이번 ELF로 판정하지 않았다. 요약의 14:18:54는 실제 LEAK 출력 시각과 다르다.

원본 일부만 허용 목록으로 추출한 `log-evidence.txt`, 부팅 구간별 태스크 이름을 적용한 `observations.json`에 근거를 보관했다.

## 3. ELF와 정확한 버전의 소스 대조

| 구성 요소 | 고정 기준 |
|---|---|
| rootstrap | 로그 및 common_dbg 문자열의 `5.4.0-rc.268` |
| Kernel | `2.0.0-rc.48` → `dd27791d16915ae2efa8fa67a797aa06bf1f774d` |
| System | `2.0.0-rc.9` → `f948ae8eabd4b4105b395dd5460a4e9531cb73ba` |
| System/da_libs submodule | `dda3e821fb27dcce228231f56638e81b9b166217` |
| product_ac | 로그 및 app1_dbg 문자열의 `d2f1acf25e1ca8c7b556c48e3ef5fcbca7f0d94c` |

기존 체크아웃은 다른 버전이므로 변경하지 않고 해당 ref의 파일을 `sources/`에 따로 저장했다. 입력 SHA-256과 소스 ref는 `manifest.json`에 있다. 버전 문자열·텍스트 VMA·심볼/명령어가 로그와 일치함을 확인했으며, 장비에서 바이너리를 다시 읽어 해시를 비교한 것은 아니다.

### 실제 할당 경로

```text
product_scube_handlerReactor, PID 117
  └─ 상위 C++ 호출자: 현재 로그로 식별 불가
      └─ app1 operator new(unsigned int), 0x0eb7d828
          └─ 0x0eb7d834: BL common malloc, 0x0e1937e8
              └─ mm_malloc
                  └─ alloc_call_addr = LR = 0x0eb7d839
```

`0x0eb7d839`의 bit 0은 Thumb 상태 비트다. 이를 제거하면 malloc 호출 다음 명령어 `0x0eb7d838`이 된다. `malloc`의 기계어는 이 LR을 `mm_malloc`에 전달하고, `heapinfo_update_node()`는 이를 기록한다. 따라서 Owner만으로 `operator new()`의 호출자까지 복원할 수 없다.

로그의 app1 text 시작 `0x0eaa9030`, common text 시작 `0x0e162010`은 ELF `.text` VMA와 이미 같다. **이 ELF에는 절대 주소를 그대로 사용한다.** 로그의 일반 안내만 보고 text base를 뺀 오프셋을 이 ELF에 입력하면 잘못 해석할 수 있다.

`binary-evidence.txt`에 관련 disassembly/DWARF를 저장했다. map에서도 `app1.map:22602`의 `libsupc++.a(new_op.o)`에 연결된다. 이는 공통 C++ 할당 wrapper이며, 제품 소스의 특정 `new` 위치를 뜻하지 않는다.

### 16바이트 DATA의 의미

```text
e0 7c 3e 61  60 07 3d 61  9f 14 73 0e  7a 00 51 61
```

해당 ELF의 DWARF는 `sizeof(mm_allocnode_s)=16`, `sizeof(mm_freenode_s)=32`인 레이아웃을 보여 준다. DATA의 시작은 할당 헤더 다음이며, free node라면 다음 필드와 겹친다.

| DATA offset | 값 | free-node 레이아웃으로 해석할 경우 |
|---|---|---|
| +0 | `0x613e7ce0` | flink |
| +4 | `0x613d0760` | blink |
| +8 | `0x0e73149f` | free_call_addr |
| +12 | `122` | free_call_pid |
| +14 | `0x6151` | reserved/padding |

`0x0e73149f`의 직전 명령어는 실제로 `cm_reactor_workqueue_type_send_task()`의 `free(event)` 호출이다. pinned [cm_reactor.c](https://github.ecodesamsung.com/TizenRT/da_libs/blob/dda3e821fb27dcce228231f56638e81b9b166217/cm_reactor/src/cm_reactor.c)의 292행, 바이너리의 `0x0e73149a`에 해당한다. 당시 PID 122는 `scube_receive_handlerReactor`다.

이는 **free 메타데이터와 일치하는 패턴**이다. 다음 두 경우를 현재 DATA만으로 구분할 수 없다.

1. 이전 사용자가 해제한 블록을 C++ 객체에 재할당했으며, 작은 객체가 덮어쓰지 않은 영역에 예전 free 정보가 남았다. `mm_malloc`은 payload를 0으로 초기화하지 않는다.
2. 실제로 해제된 블록을 검사기가 stale LEAK 상태로 출력했다.

따라서 `0x0e73149f`를 현재 누수의 할당 위치로 단정하거나, `free(event)`가 빠졌다고 결론 내릴 수 없다. 현재 Owner는 별도 헤더의 `0x0eb7d839`다. 또 요청 크기는 16보다 작을 수 있다. 정렬된 최소 블록 크기와 실제 객체 크기를 구분해야 한다.

참고로 사용자 개요의 rc.260 DATA `ee fe ee fe ff ff`도 현재 Kernel의 split remainder 표시인 `MM_REMAINDER_FREE_CALL_ADDR=0xFEEEFEEE`, PID `-1`과 일치한다. 이 값 역시 실제 함수 주소로 symbolicate할 대상이 아니다. 다만 rc.260의 정확한 레이아웃·소스까지 검증한 것은 아니다.

## 4. 별도로 재현한 검사기 결함

검사 대상 Kernel ref의 [mem_leak_checker.c](https://github.ecodesamsung.com/TizenRT/Kernel/blob/dd27791d16915ae2efa8fa67a797aa06bf1f774d/os/kernel/debug/mem_leak_checker.c)와 [mm_free.c](https://github.ecodesamsung.com/TizenRT/Kernel/blob/dd27791d16915ae2efa8fa67a797aa06bf1f774d/os/mm/mm_heap/mm_free.c)를 확인했다.

1. `fill_hash_table():208–214`는 검사 시작에 **allocated node만** hash에 등록하고 LEAK 상태로 만든다.
2. `mm_free_internal():192–196`는 allocation bit를 지우고 free 호출 정보를 쓴다. `memory_state`는 지우지 않는다.
3. `print_info():359–375`는 `memory_state == LEAK`만 보고 출력한다. **그 노드가 여전히 allocated인지 확인하지 않는다.**
4. 다음 검사도 이미 free인 노드의 state를 초기화하지 않으므로, 다른 실제 LEAK가 하나 이상 있어 출력 루프가 실행되면 이전 free node가 계속 출력될 수 있다.

제공된 ELF에서도 `0x0e0275f6`은 `memory_state`를 읽고 2와 비교한 뒤 바로 출력하며, allocation bit 검사가 없다. common `mm_free_internal`의 `0x0e193aae` 이후는 allocation bit만 지우고 free 정보를 저장한다.

### Host 재현 결과

```sh
python3 issue/analysis-20260922/reproduce_checker.py
```

```text
interleaved_free: target_allocated=0 target_state=2 printed_rows=2
next_scan: counted_live_leaks=1 printed_rows=2 stale_free_rows=1
FAIL: already-freed block is printed as a 16-byte LEAK
```

종료 코드 1. 이 스크립트는 고정 소스에서 `fill_hash_table`, `search_hash`, `print_info`, `mm_free_internal` 함수 본문을 추출한다. 검사 hash 작성 후 free가 발생하는 순서를 결정적으로 삽입한다. 32비트 타깃의 16/32바이트 헤더 레이아웃을 유지하고, 락·통계·free-list 삽입 등 OS 의존 서비스는 host stub으로 대체한다. **실제 장비의 스케줄링이나 문제 블록의 할당 경로를 재현한 것은 아니다.**

임시 host translation unit에 출력 guard만 추가해 확인했다.

```sh
python3 issue/analysis-20260922/reproduce_checker.py --guard-print
```

```text
interleaved_free: target_allocated=0 target_state=2 printed_rows=1
next_scan: counted_live_leaks=1 printed_rows=1 stale_free_rows=0
PASS: freed block is not printed (count-race remains outside this guard)
```

종료 코드 0. 제품/Kernel 파일에 fix를 적용한 것은 아니다. guard는 freed-node 출력만 막으며 검사 중 allocation 변화에 따른 counter/hash 일관성까지 해결하지 않는다.

**이번 발생 건에 대한 한계:** 원본 6개 검사의 행 개수는 모두 마지막 `N LEAKS` 값과 일치한다. 위 host의 stale-free 후속 검사에서는 둘이 달라진다. 따라서 이 재현만으로 이번 블록을 오탐이라고 판정하면 안 된다. 블록 헤더의 allocation bit 및 동시 변경 흔적을 확보해야 한다.

## 5. 남은 원인 후보와 구분 방법

| 후보 | 근거/한계 | 다음 관측에서 구분할 신호 |
|---|---|---|
| SCube가 호출한 사운드/UI C++ 경로의 실제 해제 누락 | 당시 스레드와 app1 new를 확인. 상위 callsite는 없음 | allocation bit가 계속 1이고 정확한 할당 callsite 및 대응 delete가 누락됨 |
| 작은 객체가 살아 있으나 검사기가 참조를 놓침 | checker는 allocation 시작 주소를 정확히 찾는 방식. interior/encoded pointer, 레지스터만의 참조 등은 별도 검토 필요 | 전체 RAM/TCB와 저장 포인터를 대조하면 유효한 소유자가 발견됨 |
| 검사 중 free 또는 stale LEAK의 출력 | 해당 버전에서 가능한 결함을 host 재현. 현장 행 개수는 host stale 예제와 다름 | 출력 시 allocation bit가 0이거나 free 이력이 새 할당 없이 존재함 |

사운드 경로의 구체적인 점검 지점은 pinned [product_sound_player_manager.cpp](https://github.ecodesamsung.com/TizenRT/product_ac/blob/d2f1acf25e1ca8c7b556c48e3ef5fcbca7f0d94c/apps/mediaplayer/src/product_sound_player_manager.cpp)의 `prod_soundplayer_command_play():131–150`이다. 143행의 `std::thread` 생성은 ELF에서 `0x0eac89d4`의 4바이트 `new __thread_struct`, `0x0eac89e2`의 12바이트 tuple 할당으로 이어진다. 두 요청 모두 16바이트로 보고될 수 있다. 해당 스레드는 생성 후 계속 대기하는 구조이며 로그에서도 `prod_sound_player` PID 29904가 살아 있다.

**크기가 맞는 후보일 뿐 이 두 callsite 중 하나라고 확정하지 않는다.** thread 객체는 TLS/tuple 등에 보관될 수 있고 정상 해제 경로도 바이너리에 존재한다. callback, STL container 등 다른 작은 할당도 가능하다. 이 근거만으로 `detach()`나 reactor의 `free(event)`를 수정해서는 안 된다.

## 6. 다음 장비 검증에서 필요한 최소 증거

1. **LEAK 출력과 같은 시점에 블록 헤더를 확보한다.** 현재 주소의 경우 `0x613e8c50`부터 32바이트를 확보한다. 앞 16바이트에 `preceding/alloc_call_addr/pid/memory_state/size`가 있다. `preceding & 0x80000000`으로 실제 allocated 상태를 확인한다. 이미 출력한 DATA 16바이트만으로는 불가능하다.
2. **C++ wrapper의 상위 caller를 수집한다.** 동일 바이너리에서 `operator new` 진입 `0x0eb7d828`의 LR과 요청 크기를 보존하고, malloc 반환 직후 `0x0eb7d838`의 반환 포인터와 연결한다. 이 반환 지점은 `{r4,lr}` push 이후이므로 `r0=반환 포인터`, `r4=보정된 요청 크기`, `[sp+4]=상위 caller LR`이다. 먼저 해당 스레드의 요청 크기 1–16바이트를 대상으로 한다. 주소·PID는 재부팅/재빌드마다 재확인한다.
3. **할당/해제 이력을 묶는다.** `{부팅 ID, allocation generation, ptr, requested size, alloc PID/name, caller LR, free caller, free PID}`를 고정 크기 버퍼에 기록한다. allocator 안에서 동적 할당을 하는 로그 함수를 사용하지 않는다. 현장 주소 하나를 재부팅 이후에도 고정 추적하면 안 된다.
4. **문제 블록을 구분한 뒤 수정한다.** 실제 free 누락이면 해당 상위 C++ 소유권 경로를, allocation bit 0의 오탐이면 검사기의 free-node 출력 및 스캔 일관성을 수정한다. scan 중 전역 락을 오래 잡는 방식은 별도 SMP/지연 영향 검증이 필요하다.
5. **구버전 병합 이슈는 별도로 symbolicate한다.** rc.259/260의 원본 Owner, 부팅별 ps, matching ELF가 있어야 동일 원인으로 합칠 수 있다.

## 7. 부가 관찰과 재검증

14:08의 `sched/sched_cpuselect.c:95` assertion 및 reboot는 원본에 존재한다. 이 문서는 16바이트 항목과의 인과를 입증하지 않았으므로 별도 사고로 취급한다. MsgSend 대기 로그 일부는 약 13초가 걸리는 mem_leak 실행 구간에도 나타난다. 자동 요약의 deadlock 횟수만으로 이 누수의 원인을 설명할 수 없다.

분석 결과 재확인:

```sh
python3 issue/analysis-20260922/verify_evidence.py
```

실행 결과:

```text
PASS: 5 observations, one app1 block, 16 bytes, pre-reboot PID 117 = product_scube_handlerReactor
PASS: owner 0x0eb7d839 -> app1 operator new(unsigned int), start 0x0eb7d828
PASS: payload +8 = cm_reactor_workqueue_type_send_task return from free(event), free PID 122
PASS: 6 scans, row counts [9, 10, 9, 10, 9, 6]; target absent after reboot
```

이 명령은 원본 로그와 ELF를 다시 읽어 검증하며, 새 장비 누수를 발생시키는 재현 명령은 아니다. 분석 단계의 산출물은 문서·근거·고정 소스·host 스크립트로 한정했다. 보드 테스트, 제품 코드 수정, 이슈 게시를 수행하지 않았다. 이 문서를 제외한 분석 산출물은 위에 명시한 로컬 경로에 보관한다.
