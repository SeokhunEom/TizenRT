# NuttX 12.13.0 MM 이식 — 최종 변경 및 검증 기록

검증일: **2026-10-23 KST**. 기존 TizenRT Make/Kconfig/dbuild를 유지한 상태에서 NuttX 기본 heap을 연결하고, 새 QEMU 실행의 TASH example·network_tc·kernel_tc가 모두 통과했다. 아래 결과는 flat UP multiheap 구성에 한정한다.

후속 protected/loadable 변경과 실행 결과는 [앱 수명 검증 기록](NuttX_12.13_MM_Loadable_Validation.md)에, 변경 API와 호출자 전환 방법은 [개발자 가이드](NuttX_12.13_MM_Migration_Guide.md)에 정리했다. 아래 flat 결과는 해당 시점의 source commit에 대한 기록으로 유지한다.

## 1. 기준과 최종 소스

| 항목 | 값 |
| --- | --- |
| 브랜치 | `codex/nuttx-mm-12.13` |
| TizenRT 시작점 | `94a61e7e2998ef4c4f77331d13b31986ec97b726` |
| NuttX tag / commit | `nuttx-12.13.0` / `b9b5d9afbf5a88f3c52830d4ce88ae038be0fd35` |
| 실행 검증한 source commit | `af7ce05fcfe2375d52c764979decc5634c0ccc81` |
| 검증 시 Git 상태 | clean; 이후에는 이 결과 문서와 문서 링크만 추가 |
| QEMU 지원 원본 tip | `5c0120f685ff47ab17f7ec26c0554965e312bc27` |
| 대상 | `qemu-armv8m/hello`, MPS2-AN505, flat, UP, kernel heap 2개 |
| 빌드 | `tizenrt/tizenrt:2.0.1-arm64-local`, ARM GCC 10.3, 기존 `os/dbuild.sh menu` |
| 실행 환경 | macOS Apple Silicon, QEMU 11.1.2, Homebrew Python 3.11 |
| firmware SHA-256 | `0ef865e352e963e5f62c26f55e3caff3f1933e992ec98ccfaef8a5ff26af01d1` |
| `.config` SHA-256 | `f8e3c108a1b80334e43ef3a5748cc7f5a7262958e5d827441f225556761a0d9f` |

example와 전체 suite 실행 사이에 firmware를 교체하지 않았으며, 전체 실행 후에도 두 hash가 example receipt와 일치했다. 최종 link map에는 `nxmm_mm_malloc/free/realloc/memalign`이 실제 `libmm.a`의 imported object에서 연결된다. runtime에서 출력한 backend version/revision도 고정 기준과 일치한다.

## 2. 커밋 순서와 변경 내용

**NuttX 이식 3개 → QEMU 지원 cherry-pick 27개 → 검증 example 1개 → 최종 문서 커밋** 순서다. QEMU 27개는 각각 `cherry picked from commit` trailer를 유지하며 원본 순서와 대조했다. 최종 MM 보완은 이식 커밋에 반영했고, 재배치 전후 Git tree hash가 같은지 확인했다.

| 순서 | 적용 commit | QEMU 원본 commit | 변경 내용 |
| --- | --- | --- | --- |
| 1 | `6e2e19fe92` | — | 12.13.0의 전체 mm 소스, 필요한 공개 헤더·라이선스 157개를 원본 경로에 import하고 기존 제품 코드를 분리할 기준을 확보. |
| 2 | `b65d40b7bb` | — | 12.13.0 비교·설계와 manifest/hash 검증 도구 추가. CMake 전환 없이 Make 유지 및 compat/tizenrt 경계 확정. |
| 3 | `70e53b7ae3` | — | 기본 allocator를 NuttX로 연결. mutex·IRQ·scheduler 호환, 제품 metadata·multiheap·heapinfo, 누수 snapshot, 앱 heap 정리, fault 진단과 host 회귀 검사 구현. |
| 4 | `e5eed5b72c` | `36affa38a9` | Apple Silicon용 TizenRT 2.0.1 ARM64 Docker 이미지 정의 추가. |
| 5 | `2578b0d086` | `72292507d4` | dbuild에서 로컬 ARM64 이미지를 선택하도록 연결. |
| 6 | `c623d96d26` | `2606acfcfa` | 재귀 Make에 GNU Make jobserver 전달. |
| 7 | `fe19cf29ea` | `03b7542e03` | QEMU 및 BK7239N의 host 빌드 경로 수정. |
| 8 | `0ff22e7a6c` | `85056ad924` | Apple Silicon ARM64 Docker 빌드 절차 문서화. |
| 9 | `27d61a8c3f` | `690d31f777` | GCC long-double ABI를 libc에서 일반적으로 처리. |
| 10 | `36561bbd7d` | `9116ded364` | 교차 toolchain 환경의 loadable 패키징 도구 수정. |
| 11 | `cdd5575da1` | `c065b9db6a` | Binary Manager 없는 QEMU app-separated loadable 연결 지원. |
| 12 | `3eddc2e101` | `a01d53b877` | 자식 task 수명, SMP signal 및 mqueue testcase 관련 커널 동작 보완. |
| 13 | `abd62c29b3` | `e1d40cfa6d` | QEMU ARMv8-M architecture, loader 검사 및 recipe 구성 추가. |
| 14 | `2effacaf0a` | `aa23a41172` | kernel testcase registry와 공통 ioctl wrapper 정비. |
| 15 | `0ec2584cec` | `43770a916d` | QEMU kernel-test 자동 runner 추가 및 검증 조건 강화. |
| 16 | `b65f5d6a74` | `5bf59e3a67` | ARMv8-M target별 역할과 검증 경계 문서화. |
| 17 | `3a8725c0f0` | `67f11179eb` | dbuild menu와 자동 Docker platform 선택 절차 문서화. |
| 18 | `4d3301e46b` | `703b1efaa8` | QEMU SRAM multiheap 및 영속 A/B runtime 지원. |
| 19 | `fd22bf4b01` | `84709d01eb` | SRAM 배치와 runtime 경계 문서화. |
| 20 | `d96b860f0c` | `137cfe4128` | semaphore holder accounting과 다중 heap 사용량 보고 보완. MM 통합 충돌은 NuttX backend와 제품 accounting을 유지하며 해결. |
| 21 | `08cd7914b7` | `7601e8fe66` | exFAT bind mount에서 일시적인 복사 경쟁을 한 번 재시도. |
| 22 | `a5cce5c0ee` | `636597b1d1` | 자동 runner가 kernel assertion을 발견하면 즉시 실패 처리. |
| 23 | `adb8100ab7` | `6abc8fd971` | QEMU kernel-test 실행 절차와 결과 해석 문서화. |
| 24 | `5b62a7d9fe` | `9ee9cbf082` | QEMU TASH·SmartFS·heap 진단 설정 정렬. |
| 25 | `25e1efcb33` | `a3dc62131a` | LAN9118 Ethernet runtime 지원 추가. |
| 26 | `fd319a8b86` | `d63eb1d9e3` | Ethernet network testcase, 자동 검증 및 문서 연결. |
| 27 | `db23135f46` | `fc907ccb72` | 사람용 recipe 비교 및 변경 이력 문서 추가. |
| 28 | `7bfc345b2b` | `b5eacbdc04` | QEMU 안내 링크와 main 기준선 설명 보완. |
| 29 | `442d6039ec` | `82ed01c107` | QEMU timer에서 경과한 tick을 반영. |
| 30 | `2a4bc1cab3` | `5c0120f685` | TCSETS 이후 UART interrupt 설정 보존. |
| 31 | `af7ce05fcf` | — | TASH nuttx_mm_test 5개 case와 fresh-run helper 추가. 기존 MM testcase를 새 accounting 계약에 맞춰 보완하고 QEMU 검증 설정·memory-region 헤더·누수 TC 심볼 충돌 수정. |

마지막 문서 전용 커밋은 이 기록과 설계·비교 문서의 링크 및 최종 제한을 갱신한다. 해당 커밋은 검증한 실행 소스를 변경하지 않는다.

검증 example 커밋에는 다음 보완도 포함한다.

- `tc_umm_heap`의 구형 allocator 고정 overhead 가정을 제거하고, arena 불변·사용/여유량 증감 일치·해제 후 baseline 복구·PID 소유권·할당 개수를 확인한다. calloc zero-fill과 memalign 정렬도 검사한다.
- heapinfo group 검사는 제품 metadata의 논리 크기 대신 `mm_allocation_charge()`의 실제 chunk 비용을 사용한다.
- 현재 커널의 `pthread_cond_timedwait`는 watchdog 인자 3개를 전달한다. 기존 QEMU의 `CONFIG_MAX_WDOGPARMS=2`에서는 timer가 시작되지 않아 기다리던 TC가 멈췄다. 3으로 수정한 뒤 clean build했다.
- TASH 앱과 TC에 동일한 `mem_leak_checker_main` 심볼이 있어 실제 TC 대신 앱이 연결될 수 있었다. TC를 `tc_mem_leak_checker_main`으로 분리하고 선언·registry·host 계약 검사도 갱신했다. 최종 map과 serial에서 실제 TC 실행을 확인했다.
- QEMU linker symbol을 사용하는 `memory_region.h`를 추가하여 누수 검사의 data/BSS 경계를 연결했다.

## 3. NuttX 원본과 달라진 부분

[manifest](../tools/nuttx-sync/modules/mm.json)는 고정 tag의 원본 hash와 현재 허용 hash를 함께 기록한다. 최종 결과는 **155개 원본 일치 / 2개 명시적 변경 / 실패 0**이다. imported CMake 파일도 보관하지만 빌드에는 사용하지 않는다.

| 구분 | 파일 또는 계층 | 차이와 이유 |
| --- | --- | --- |
| 원본 파일 수정 | `os/mm/Makefile` | 기존 TizenRT library 및 kernel/user phase 유지. imported object에만 private header·symbol mapping 적용. |
| 원본 파일 수정 | `os/mm/Kconfig` | TizenRT 설정 인터페이스와 기본 backend 선택 유지. 미통합 옵션을 지원된 것처럼 활성화하지 않음. |
| 원본 수정 없이 동작 보완 | `compat/symbol_map.h` | imported `memcpy`를 `memmove`로 매핑. 12.13.0 realloc의 아래 방향 이동에서 실제 overlap이 발생하는 host 회귀 사례를 처리. |
| 원본 수정 없이 동작 보완 | `tizenrt/bridge/nuttx_heap_bridge.c` | `mm_extend`의 임시 tail block을 free하기 전에 사용량을 보정하여 peak/accounting wrap 방지. |
| 추가 제품 계층 | `tizenrt/mm_api.c`, `kmm_heap/`, `umm_heap/` | 기존 함수 서명·caller 인자·heap 선택 유지. NuttX control block은 opaque pointer로 연결하고 실제 할당·분할·병합은 upstream에서 처리. |
| 추가 제품 계층 | metadata prefix, heapinfo·누수 검사 | caller/PID·allocation 목록·실제 chunk 비용을 별도 보관. 기존 Tizen node와 NuttX node를 cast하지 않음. 잠긴 상태에서 snapshot을 만들고 출력은 lock 밖에서 수행. |
| 외부 호출자 통합 | binfmt, ARM assert, watchdog 진단 | heap 회수 전에 lock과 지연 free 정리. 새 node 계약에 맞춘 진단. 검사 불가와 실제 손상을 구분하고 fault 상황에서 잠금을 기다리지 않음. |

**소스 bytes 보존과 실행 의미의 완전한 동일성은 다르다.** 위 두 bridge 보완과 TizenRT 정책은 의도한 동작 차이다. metadata prefix·정렬 여유·추가 잠금에 비용이 있으므로 기존 allocator와 같은 메모리 사용량이나 성능을 주장하지 않는다. physical mallinfo는 backend chunk를, per-PID 값은 제품 진단용 논리 크기를 다룬다.

## 4. compat에 남은 임시 처리

| 임시 provider | 현재 처리 | 제거하거나 교체할 조건 |
| --- | --- | --- |
| `nuttx_os.c` + `nuttx/mutex.h` | Tizen semaphore로 recursive mutex·owner·PI 경로 제공. EINTR 재시도, errno 보존, 마지막 unlock까지 cancellation 금지. 예상 못한 blocking lock 실패는 성공으로 숨기지 않음. | native NuttX mutex/semaphore 이식 후 같은 오류·취소·수명 계약 검증. |
| `nuttx/arch.h`, `nuttx/irq.h` | UP IRQ 감지와 critical section. protected 사용자 경로는 privileged IRQ 명령을 실행하지 않음. | 공통 arch/IRQ provider 이식. |
| `nuttx/sched.h` | boot task ID와 task_exit 전환 상태를 구분. ready head가 RUNNING이 아니면 `-ESRCH`로 지연 free 유도. | scheduler/task identity 및 종료 전환 계약 이식. |
| `nuttx/config.h` | 기본 heap·8-byte alignment·region capacity를 현재 설정에 매핑하고 미통합 feature를 명시적으로 거부. | 각 feature provider와 config별 검증 확보. |
| `malloc.h`, `nuttx/userspace.h` | NuttX mallinfo/heap identity를 private ABI로 격리. 할당 실패 정책은 Tizen 계층이 담당. | public ABI 또는 userspace/heap registry 통합. |
| `nuttx/fs/procfs.h`, `nuttx/sched_note.h` | 비활성 procfs heap 등록·trace만 제외. 활성 feature를 빈 성공 함수로 처리하지 않음. | 실제 procfs·trace provider 구현. |
| `symbol_map.h` | `nxmm_*` namespace와 overlap-safe copy를 imported object에 한정. | 기존 ABI 충돌 제거; realloc copy는 upstream 수정·검증 후 해제. |
| compiler/math/system header shims | imported object에서 필요한 컴파일 속성·정렬·수학 helper를 좁게 제공. | 공통 header와 ABI가 맞아질 때 제거. |

제품의 heap 선택, caller/PID, heapinfo, 앱 heap 수명 정책은 `tizenrt/`에 남는다. 모든 wrapper를 임시 코드로 분류해 나중에 일괄 삭제하는 구조가 아니다. 세부 계약은 [compat README](../os/mm/compat/README.md)와 [TizenRT README](../os/mm/tizenrt/README.md)를 따른다.

## 5. 최종 검증 결과

| 검사 | 결과 | 근거/범위 |
| --- | --- | --- |
| Make firmware build | PASS | kernel image 521,252 / 655,360 bytes, size gate 통과. 빌드 경고가 전혀 없다는 의미는 아님. |
| 원본 hash 검사 | PASS | 155 unchanged, 2 documented changes, failures=[]; upstream Git object와 비교. |
| 실제 upstream host core | PASS | ASan+UBSan, malloc/calloc/realloc/alignment/OOM·overflow, 4-thread 경합, multi-region·extend, corruption·quiescent 검사. |
| host registry/wrapper 계약 | 30 PASS | execution ownership 5, ioctl wrapper 4, optional registry 6, capability 15. |
| TASH `nuttx_mm_test` | **5 PASS / 0 FAIL** | fresh QEMU, runtime version/SHA 확인, 종료 후 task 소멸 확인. |
| TASH `kernel_tc` | **460 PASS / 0 FAIL** | 완료 marker 확인 및 runner의 자동 assertion 검사 통과. |
| TASH `network_tc` | **161 PASS / 0 FAIL** | DHCP, IPv4/IPv6 gateway, DNS 포함. |

MM example 세부:

| case | checks | 확인 내용 |
| --- | ---: | --- |
| public | 445 | malloc, calloc zero-fill, realloc grow/shrink·데이터 보존, alignment |
| failure-preserves-data | 13 | overflow·OOM·실패 후 데이터 보존, 잠긴/유휴 heap의 모의 fault 진단과 lock/cancel 상태 보존 |
| multiheap | 625 | heap 2개에 지정 할당, ownership·정렬·데이터 |
| fragmentation | 19,672 | 고정 seed, 1,024 steps, 재할당·해제와 데이터 패턴 |
| threaded | 79,043 | pthread 4개, 동시 할당·재할당·해제, join |

fault 검사는 IRQ를 막고 `abort_mode`를 잠시 설정한 모의 검사다. 실제 panic/crash dump 또는 IRQ handler에서 해제하는 target 검증은 아니다. host의 IRQ/task-exit 지연 free 검사는 host adapter를 사용하므로 target ordering의 증거로 확장하지 않는다.

누수 검사는 최종 로그에 `[tc_mem_leak_checker_kernel] PASS`가 정확히 한 번 있다. snapshot에는 같은 4-byte allocation 4개가 두 번 LEAK로 보고되었다. 따라서 이 결과는 **누수 검사 실행 경로의 통과**이며 시스템에 누수가 없다는 증거는 아니다. 없는 binary/app 이름·빈 이름을 거부하는 negative test의 오류 출력은 예상한 결과다.

네트워크는 IPv4 gateway와 IPv6 gateway가 각각 3/3, `example.com` DNS가 성공했다. 외부 `1.1.1.1` ICMP는 0/1이며 runner가 허용하는 관측값이다. 일반적인 인터넷 연결 성공으로 확대 해석하지 않는다.

## 6. 재현 방법과 artifact

저장소 루트에서 실행한다. build 설정은 `os/`에서 Homebrew Bash로 menu를 열어 `5`로 `qemu-armv8m/hello`를 설정하고 `1`로 빌드한다. menu 입력은 단계별로 제공한다. Docker platform은 dbuild가 선택한다.

```bash
cd os
/opt/homebrew/bin/bash ./dbuild.sh menu
```

다음 명령은 저장소 루트에서 실행한다. output directory는 새 경로여야 한다.

```bash
python3 tools/nuttx-sync/verify.py --upstream /path/to/nuttx
/opt/homebrew/bin/bash os/mm/compat/tests/run.sh

/opt/homebrew/bin/python3.11 apps/examples/nuttx_mm_test/run_qemu.py \
  --output build/nuttx-mm-validation/new-mm-run --timeout 180

/opt/homebrew/bin/python3.11 .github/scripts/qemu-armv8m-kernel-tc.py \
  --config hello --timeout 1200 \
  --log build/nuttx-mm-validation/new-kernel-tc.log \
  --result build/nuttx-mm-validation/new-kernel-tc.json
```

수동 TASH에서는 `help`, `nuttx_mm_test`, `ps`, `network_tc`, `kernel_tc`를 실행할 수 있다. network_tc의 DHCP/gateway 준비와 전체 자동 절차는 [QEMU 실행 가이드](AI/Mac_QEMU_ARMv8M_TASH_KernelTC.md)를 따른다.

이번 로컬 artifact는 `build/nuttx-mm-validation/` 아래에 보관한다.

- `source-verification.json`, `host-core.log`, `registry-tests.log`
- `hello-final-fault/result.json`, `hello-final-fault/serial.log`
- `hello-final-fault-kernel-tc.json`, `hello-final-fault-kernel-tc.log`
- 실행 ELF와 map은 `build/output/bin/tinyara`, `tinyara.map`; 빌드 로그는 `os/build.log`

큰 로그와 firmware는 Git에 넣지 않는다. 최종 실행의 주요 receipt 및 로그 hash는 [기계 판독용 검증 기록](validation/nuttx-mm-12.13-final.json)에 보존한다. 전체 kernel runner의 `returncode:null`은 완료 marker 확인 후 QEMU를 정리하는 동작이며 QEMU 자체의 정상 종료 코드 0을 뜻하지 않는다.

## 7. 남은 지원 범위

- **실행 검증 완료:** 기본 NuttX heap, flat UP, QEMU ARMv8-M multiheap 및 선택한 heapinfo/누수 검사 구성.
- **소스 연결 후 실행 미검증:** protected/loadable 앱 수명·unload/reload·MPU 분리, 다른 architecture/실제 보드, 실제 fault dump. QEMU 지원 커밋에 다른 recipe가 있어도 이번 이식 검증 결과로 계산하지 않는다.
- **명시적 미지원:** SMP, KASAN/TLSF/mempool, GRAN/PGALLOC/MMU, free-history/UAF, large-back placement, Binary Manager 강제 복구, NuttX heap trace/backtrace 관련 선택 기능. 소스 보관과 runtime 활성화는 다르다.
- `MM_DUMP_CORRPUTED_HEAP`은 bounded fault-safe raw dump provider가 필요하다. NuttX dump/panic-on-failure는 native heap identity 연결이 필요하다. 현재는 해당 옵션을 빌드에서 거부한다.
- metadata 및 잠금 비용의 성능·최대 가용 메모리 비교는 아직 측정하지 않았다.

다음 모듈 이식 시 compat provider를 계약 단위로 교체하고, 이 문서의 소스 hash·host·TASH 검증을 다시 수행한다. 설계의 출발점은 [모듈 동기화 설계](NuttX_Module_Sync_Design.md), 이식 전 구조 차이는 [커널 비교](NuttX_12.13_Kernel_Comparison.md)에 있다.
