# NuttX 모듈 동기화 구조 설계 — 12.13.0 mm 우선

최종 갱신: 2026-10-23. 이 문서는 구현 방향과 검증 범위를 정의한다. 실제 빌드·실행 결과와 커밋별 변경은 [최종 검증 기록](NuttX_12.13_MM_Validation.md)에 정리했다.

## 1. 확정한 요구사항

1. 기본 구현은 **Apache NuttX 12.13.0**과 동일하게 가져온다.
2. TizenRT 특화 기능·디버깅·로그는 필요한 범위에서 유지·변경한다.
3. 아직 이식하지 않은 커널 기능은 원본을 광범위하게 수정하는 대신 **추가 호환 코드**로 임시 제공한다.
4. 첫 모듈은 `mm`이다. semaphore·scheduler 전체 이식을 선행 조건으로 두지 않는다.
5. NuttX 원본은 `os/mm/`에 직접 배치하고, 임시 호환은 `compat/`, 제품 확장은 `tizenrt/`로 분리한다.
6. **TizenRT의 Make/Kconfig/dbuild 빌드 시스템을 유지한다.** CMake로 전환하지 않는다.
7. 커밋은 **NuttX 이식 → QEMU 지원 cherry-pick → 검증 example** 순서로 구성한다.

고정 기준:

| 대상 | 기준 |
| --- | --- |
| NuttX | `nuttx-12.13.0`, `b9b5d9afbf5a88f3c52830d4ce88ae038be0fd35` |
| TizenRT 시작점 | 로컬 `master`, `94a61e7e2998ef4c4f77331d13b31986ec97b726` |
| 작업 브랜치 | `codex/nuttx-mm-12.13` |
| QEMU 지원 원본 | `codex/qemu-armv8m-kernel-tc`; 가져온 커밋 목록과 SHA는 Git 이력에 보존 |
| 첫 실행 대상 | `qemu-armv8m/hello`: 실제 TASH와 multiheap allocator |

이전 13.1.0 비교·설계는 이 작업의 기준이 아니다. 특히 12.13.0은 `CONFIG_SYSTEM_TIME64`에 따라 unsigned 32/64-bit `clock_t`를 사용한다. 버전 번호만 치환하지 않고 실제 tag의 헤더와 구현을 확인했다.

## 2. 소스 배치와 소유권

```text
os/mm/
  mm_heap/                 # NuttX 기본 allocator 원본
  mm_gran/                 # NuttX granule allocator 원본
  kmm_heap/                # NuttX kernel wrapper 원본
  umm_heap/                # NuttX user wrapper 원본
  ...                      # 12.13.0의 나머지 mm 모듈
  Makefile                 # TizenRT Make 연결: 명시적인 원본 차이
  Kconfig                  # TizenRT 설정 연결: 명시적인 원본 차이
  compat/                  # 미이식 OS 기능과 구형 mm 호출 규약
    include/               # imported object만 사용하는 호환 헤더
    ...                    # mutex, IRQ, scheduler, private symbol mapping
  tizenrt/                 # multiheap, 앱 heap, heapinfo, 진단·로그
    ...                    # 원본의 구조를 읽는 좁은 bridge 포함

os/include/nuttx/mm/        # manifest로 추적하는 upstream 공개 헤더
os/include/tinyara/mm/      # 기존 TizenRT 호출자용 interface

tools/nuttx-sync/           # 고정 SHA, 파일 대응, checksum, 허용 차이 검증
apps/examples/nuttx_mm_test/ # 마지막 검증 단계에서 추가하는 TASH example
```

원본은 별도 `os/upstream/nuttx/`에 중복 보관하지 않는다. NuttX `mm/<path>`는 `os/mm/<path>`로 대응한다. 필요한 공개 헤더와 라이선스도 원본 경로·목적 경로·hash를 추적한다. 최초 import의 대상 파일은 157개이며, 실제 목록은 동기화 manifest가 기준이다.

파일 관리 원칙:

- **원본**: 고정 Git object와 byte-for-byte 동일하게 보존한다. namespace와 include 이름, 포맷을 일괄 치환하지 않는다.
- **허용 차이**: TizenRT 빌드 연결, 제품 기능 또는 진단을 위한 변경은 구체적 diff와 이유를 기록한다. 파일 전체를 검증에서 제외하지 않는다.
- **추가 코드**: 임시 호환과 제품 기능을 분리한다. 이전 allocator로 allocation/free를 우회하지 않는다.
- **소스와 실행 범위**: 모든 `mm` 소스를 보관해도 모든 allocator·설정이 활성화되거나 검증된 것은 아니다.

CMakeLists.txt와 NuttX Make.defs 등은 소스 동기화 대상으로 보관할 수 있지만, 이 작업의 빌드 진입점은 기존 TizenRT Make다. 기존 라이브러리 생성과 kernel/user phase 구성을 유지하며 필요한 source 목록과 object 전용 flags를 연결한다.

## 3. 호출 방향

```text
TizenRT malloc/calloc/realloc/memalign 및 mm_* 호출자
        ↓
TizenRT wrapper: heap 선택, caller/PID, 앱 수명, 진단
        ↓
NuttX 12.13.0 mm_heap 기본 구현
        ↓
compat: NuttX가 요구하는 mutex / IRQ / scheduler interface
        ↓
현재 TizenRT 커널
```

한 heap의 메모리는 하나의 backend가 소유한다. 기존 allocator node를 NuttX node로 cast하지 않는다. 초기화·할당·재할당·정렬 할당·해제를 모두 같은 backend로 연결한다.

| 계층 | 책임 | 향후 제거/유지 |
| --- | --- | --- |
| NuttX 원본 | 분할, 병합, 정렬, region, heap/node 관리 | upstream 동기화하며 유지 |
| `compat/` OS adapter | 미이식 잠금·IRQ·task API 계약 제공 | 해당 NuttX 모듈 이식 시 실제 provider로 전환·제거 |
| `compat/` 구형 호출 adapter | 기존 mm 함수의 인자·반환값 연결 | 호출자 전환 후 제거 가능 |
| `tizenrt/` 제품 wrapper | multiheap 선택, 앱 heap 수명, allocation failure 정책 | 제품 기능으로 유지 |
| `tizenrt/` 진단/bridge | caller/PID, heapinfo, node 조회·순회 | 제품 요구가 유지되는 동안 보존 |

처음부터 helper마다 파일을 만들 필요는 없다. 다만 임시 커널 호환과 영구적인 제품 정책을 한 파일에 계속 섞지 않는다.

## 4. 12.13.0에서 실제로 달라진 계약

`mm_initialize_heap(const struct mm_heap_config_s *)`는 새 heap 포인터를 반환한다. 공개 `mm_initialize(name, start, size)`는 이를 호출하는 inline convenience 함수다. `config.heap == NULL`이면 제공된 arena에서 control block 공간을 확보하고, non-NULL이면 지정된 heap에서 control block을 할당한다. 기존 TizenRT의 `mm_initialize(existing_heap, start, size)`와 메모리 소유권이 다르다.

또한 NuttX node는 `size`의 하위 두 비트에 현재 할당 상태와 앞 node의 free 상태를 저장한다. TizenRT의 `preceding` 상위 allocation bit 해석을 그대로 적용할 수 없다. heapinfo와 누수 검사 등 직접 구조체 접근은 bridge 또는 새로운 node 계약에 맞춰 수정한다.

| 충돌 지점 | 처리 |
| --- | --- |
| `mm_malloc(heap, size, caller)`와 upstream 2인자 API | object 전용 symbol mapping과 TizenRT wrapper |
| 공개 `struct mm_heap_s`와 upstream private layout | public handle/제품 metadata와 upstream backend를 구분 |
| 앱 로더의 heap control block 크기·주소 계산 | 실제 layout과 arena 경계를 한곳에서 계산 |
| kernel/user/app heap 선택 | 제품 wrapper가 담당하고 실제 allocation은 upstream 호출 |
| heapinfo·누수 검사·task 종료 | 새 node 해석, 잠금, deferred free 수명에 맞춰 통합 |

원본 타입 및 심볼 충돌은 imported object와 bridge에 한정한 prefix/header 환경으로 처리한다. 일반 TizenRT compilation unit에 NuttX 시스템 헤더를 전역 우선 적용하지 않는다. 같은 심볼의 임시·실제 provider를 동시에 링크하거나 weak/link 순서로 선택하지 않는다.

## 5. 임시 호환 코드의 동작 조건

- mutex는 오류 반환, owner, 재진입, PI, signal 재시도와 errno 보존을 실제 mm 호출자의 기대에 맞춘다. `nxmutex_lock`을 `sem_wait`의 이름만 바꾼 별칭으로 만들지 않는다.
- `mm_lock()`은 UP IRQ에서 잠금이 비어 있을 때만 접근하고, SMP IRQ에서는 지연 해제 경로로 돌린다. adapter가 이 조건을 무조건 성공으로 바꾸지 않는다.
- `_SCHED_GETTID()`는 context switch/종료 중 바로 해제할 수 없는 상태를 보존한다. thread stack을 해제하는 순간의 실행 컨텍스트와 ready-list head가 항상 같은 것은 아니다.
- deferred free는 실제 안전한 시점에 처리하고, 앱 arena를 회수하기 전에 해당 heap을 참조하는 목록을 정리한다.
- heap bootstrap과 registry는 초기화하려는 heap 자신의 malloc에 의존하지 않는다.
- heap 잠금 안의 로그/진단은 재귀 malloc과 다른 heap의 lock 순환을 만들지 않는다.
- 비활성 기능의 upstream no-op만 허용한다. 활성 KASAN/trace/procfs 기능을 빈 성공 stub으로 처리하지 않는다.

각 adapter에는 소비자, 제공 interface, 대체할 모듈과 제거 조건을 기록한다. 후속 모듈 이식 커밋에서는 provider 교체와 해당 adapter 제거 및 영향받는 heap 재검증을 함께 한다.

## 6. 이번 구현·검증 범위

첫 runtime backend는 **default `mm_heap`**이다. NuttX `kmm/umm` wrapper, granule, TLSF, mempool, KASAN, IOB, shared/mapped memory 소스의 보관과 실제 활성화는 구분한다. 선택하지 않은 기능을 검증 완료로 보고하지 않는다. 지원하지 않는 조합은 build/config 단계에서 드러나게 하고 기존 제품 config를 조용히 다른 의미로 바꾸지 않는다.

TASH 검증은 실제 public allocation 경로로 수행한다.

- runtime backend version/revision과 예상 12.13.0 SHA 확인.
- malloc/calloc zero-fill, realloc grow/shrink 및 데이터 보존, memalign.
- 지정 heap 할당과 heap ownership 확인.
- 고정 seed의 fragmentation 반복 및 각 block의 데이터 패턴 확인.
- 여러 pthread의 동시 할당/재할당/해제와 join.
- TASH에서는 실제 public allocation의 heap에서 overflow·OOM 실패와 기존 데이터 보존을 검사한다. 독립 arena의 병합·회수·확장은 별도 호스트 검사로 확인한다.
- example 후 TASH 재진입 및 기존 kernel TC 회귀 시험.

QEMU `hello` 통과는 flat UP multiheap 구성의 증거다. loadable 앱 수명·앱 분리, SMP/IRQ ordering, 실제 보드, 모든 debug 옵션 또는 성능 향상은 별도 증거가 필요하다. 결과는 PASS/FAIL/미검증으로 나누고 과거 QEMU 브랜치의 통과 수치를 이번 이식 결과로 재사용하지 않는다.

## 7. 커밋 구성과 완료 판정

1. **NuttX 이식 커밋들**: 최종 설계·12.13 비교, 원본 import/manifest, Make/Kconfig/private headers, kernel 호환, TizenRT wrapper와 진단 통합. 원본 import와 수정을 구분할 수 있게 유지한다.
2. **QEMU 지원 cherry-pick**: 사용자 지정 브랜치의 지원 커밋을 순서대로 적용한다. 충돌 해결은 이식된 allocator를 유지하며 QEMU board·runner 동작을 보존한다.
3. **검증 example 커밋들**: TASH test와 Kconfig/Make 등록 및 필요한 검증 config. 새 firmware를 빌드하고 fresh serial 로그에서 결과를 확인한다.
4. **최종 결과 기록**: 실제 실행한 config, source SHA, 빌드·TASH·test 결과와 남은 제한을 기록한다.

소스 동기화는 manifest/hash 및 허용 diff 검증으로 판정한다. runtime 전환은 최종 link의 선택된 backend와 실기동 테스트로 판정한다. old allocator가 연결된 상태에서 example만 통과한 결과를 NuttX 검증으로 계산하지 않는다. 생성된 firmware와 큰 serial 로그는 검증 artifact로 보관하며 일반 소스 커밋에 넣지 않는다.

## 8. 구현된 호환 경계

- 원본 157개 중 155개는 고정 tag와 byte-for-byte 동일하다. `os/mm/Makefile`과 `os/mm/Kconfig`만 기존 빌드 연결을 위해 변경하고, 현재 파일의 hash와 이유를 manifest에 기록한다.
- `compat/nuttx_os.c`는 TizenRT semaphore로 mutex를 제공한다. 메모리 변경 중 cancellation을 막고, IRQ·task 종료 전환 중 해제는 안전한 thread context까지 지연한다.
- TizenRT의 caller/PID 및 live allocation 목록은 별도 prefix에 저장한다. prefix와 정렬 여유 공간은 실제 메모리 비용이며, 이전 allocator와 동일한 footprint 또는 성능을 주장하지 않는다.
- `mm_mallinfo`는 heap 하나의 snapshot을 반환하고, public wrapper가 다중 heap을 합산한다. per-PID 논리 크기와 실제 chunk 비용은 별도 계약으로 문서화한다.
- 누수 검사는 필요한 storage를 먼저 확보하고, 잠근 allocation을 snapshot으로 복사한 뒤 lock 밖에서 출력한다. 사용 중인 heap을 얻지 못하면 완전한 결과를 만든 것처럼 보고하지 않는다.
- 원본의 overlapping `memcpy` realloc 경로와 `mm_extend`의 임시 allocation 사용량 accounting은 호스트 회귀 검사로 재현했다. 전자는 imported object 전용 `memmove` mapping, 후자는 bridge의 accounting으로 처리하여 upstream 원본 diff를 만들지 않는다.
- native heap과 bridge의 lock은 heap RAM 회수 전에 파괴한다. protected/loadable 앱 경로는 소스 연결 단계이며, 첫 실행 검증 범위는 flat UP 구성이다.
- fault/IRQ 상황에서는 잠금을 기다리지 않고, UP에서 IRQ를 막고 두 heap lock이 모두 비어 있을 때만 bounded corruption 검사를 수행한다. 검사 불가(`-EAGAIN`)와 실제 손상(`-EFAULT`)을 구분하여 ARM assert 경로가 검사 불가만으로 crash report를 중단하지 않게 한다. TASH에서는 잠긴 heap과 유휴 heap의 모의 fault 검사 및 잠금·취소 상태 보존을 확인했다. 실제 panic/crash dump 검증은 별도다.

세부 계약과 제거 조건은 [compat 설명](../os/mm/compat/README.md), [TizenRT 계층 설명](../os/mm/tizenrt/README.md)에 있다. SMP, KASAN/TLSF/mempool, GRAN/PGALLOC/MMU, free-history/UAF, large-back placement, Binary Manager 강제 복구 등 미지원 조합은 명시적으로 거부한다. `MM_DUMP_CORRPUTED_HEAP`에는 fault-safe raw dump provider가, NuttX dump/panic-on-failure에는 native heap identity 연결이 필요하므로 현재 활성화하면 빌드를 거부한다.

## 9. 근거

- [12.13.0 비교](NuttX_12.13_Kernel_Comparison.md)
- [NuttX 공개 mm API](https://github.com/apache/nuttx/blob/b9b5d9afbf5a88f3c52830d4ce88ae038be0fd35/include/nuttx/mm/mm.h)
- [NuttX 초기화](https://github.com/apache/nuttx/blob/b9b5d9afbf5a88f3c52830d4ce88ae038be0fd35/mm/mm_heap/mm_initialize.c), [잠금](https://github.com/apache/nuttx/blob/b9b5d9afbf5a88f3c52830d4ce88ae038be0fd35/mm/mm_heap/mm_lock.c), [node/heap](https://github.com/apache/nuttx/blob/b9b5d9afbf5a88f3c52830d4ce88ae038be0fd35/mm/mm_heap/mm.h)
- [NuttX 기본 allocator source 선택](https://github.com/apache/nuttx/blob/b9b5d9afbf5a88f3c52830d4ce88ae038be0fd35/mm/mm_heap/Make.defs)
