# NuttX 12.13.0 MM — protected/loadable 검증

검증일: **2026-10-23 KST**. 기존 flat 이식 이후 QEMU ARMv8-M의 protected `loadable_all`과 `loadable_apps`를 clean build하고, 실제 Binary Manager 앱 로드 → 정상 종료 → 재로드 과정의 heap 회수를 검사했다. 호출자 전환 방법은 [개발자 가이드](NuttX_12.13_MM_Migration_Guide.md), 이전 flat 결과는 [기존 검증 기록](NuttX_12.13_MM_Validation.md)을 따른다.

## 1. 기준과 커밋

브랜치는 `codex/nuttx-mm-12.13`, 이번 작업 직전 원격 tip은 `90ddd88ead805843ef9baa73f6ef9cdbbb519c48`이다. 기존 **NuttX 이식 → QEMU cherry-pick → example** 이력은 유지하고 다음 커밋을 뒤에 추가했다.

| 커밋 | 변경 내용 |
| --- | --- |
| `deec52b99` | protected kernel/user 공유 pending-free head의 원자 연산, app heap routing, 모든 task group 종료 이후의 지연 회수, mutex/registry 정리 후 section RAM 반환, PID 재사용·중복 free·callback 순회 문제 수정. production queue와 exec 소유권 host 검사 포함. |
| `5363bc644` | 두 protected recipe, 실제 app2 할당 workload, TASH `nuttx_mm_life`, kernel 관측 driver와 3회 lifecycle runner 추가. |
| `14c3ca558` | TASH 명령 전송 전에 app1 로드 완료와 app2 warmup 종료 출력을 모두 기다리도록 runner 보완. 펌웨어 소스 변경 없음. |
| 이 문서를 추가한 후속 문서 커밋 | 검증 결과·기계 판독 receipt, 시스템/앱 개발자 마이그레이션 가이드, 설계·README 지원 범위 갱신. |

NuttX 기준은 **12.13.0**, SHA `b9b5d9afbf5a88f3c52830d4ce88ae038be0fd35`다. 원본 검사 결과는 **155 unchanged / 2 documented changes / failures=[]**다. 이번 후속 작업은 upstream 소스를 추가 수정하지 않았다. 기존 두 차이는 `os/mm/Makefile`과 `os/mm/Kconfig`의 TizenRT 빌드 연결이다. 원본은 `os/mm/`, 임시 OS 호환은 `os/mm/compat/`, 제품 정책은 `os/mm/tizenrt/`에 유지한다. CMake로 전환하지 않았다.

## 2. 구성과 최종 결과

두 구성 모두 UP, protected, kernel heap 3개, common binary와 앱 2개를 사용한다. `loadable_apps`는 `CONFIG_XIP_KERNEL=y`이고 앱은 ELF 로드 방식이다. XIP 앱인 `xip_all`은 이번 검증 대상이 아니다.

| 검사 | loadable_all | loadable_apps |
| --- | --- | --- |
| 기존 dbuild/Make clean build 및 패키지 크기 gate | PASS | PASS |
| TASH 실제 앱 load/normal-exit/reload | **3회 PASS** | **3회 PASS** |
| main → 마지막 pthread → 별도 task group 종료 | 3회 확인 | 3회 확인 |
| heap 소유권·mutex teardown·registry/common slot·지연 참조 제거 | 3회 확인 | 3회 확인 |
| 모든 kernel heap의 arena/used/free baseline 복귀 | 3회 일치 | 3회 일치 |
| lifecycle controller 종료 후 `ps`에서 소멸 | PASS | PASS |
| `kernel_tc` | **447 PASS / 0 FAIL** | **447 PASS / 0 FAIL** |
| `network_tc` | **161 PASS / 0 FAIL** | **161 PASS / 0 FAIL** |
| gateway IPv4/IPv6 ping | 각 3/3 | 각 3/3 |
| DNS | PASS | PASS |

두 빌드의 크기 gate는 kernel 242,228 / 655,360 bytes, common 570,846 / 786,432, app1 1,689 / 393,216, app2 3,825 / 131,072다. 이 수치는 패키지 크기이며 heap 여유량이나 성능 지표가 아니다. 빌드 경고가 전혀 없다는 의미도 아니다. 외부 public ICMP 응답은 QEMU user networking의 필수 통과 조건에 포함하지 않는다. 실제 응답 수치는 receipt에 보존한다.

빌드는 macOS Apple Silicon에서 `tizenrt/tizenrt:2.0.1-arm64-local`, ARM GCC 10.3.1을 사용했다. 실행은 QEMU 11.1.2 MPS2-AN505와 Homebrew Python 3.11이다.

## 3. heap 회수 판정 방법

부팅 때 자동 실행되는 app2는 warmup이다. 이후 TASH의 `nuttx_mm_life`가 Binary Manager를 통해 app2를 새로 3회 로드한다. 실제 앱은 public `calloc/realloc/memalign/free`와 두 pthread의 각 256회 allocation/data 검사를 실행한다.

메인은 **257 bytes를 의도적으로 남긴 채 정상 return**한다. 같은 group의 detached pthread가 뒤늦게 할당·해제한 다음 종료하고, `task_create()`로 만든 별도 group은 두 PID가 사라진 뒤 할당·해제하고 마지막에 종료한다. 각 단계의 live allocation 257/521/733 bytes 양끝 주소가 이번 app2 heap에 속하는지 privileged registry 조회로 확인한다. 이는 해당 주소의 heap 소유권 확인이며 MPU의 모든 보안 격리 동작에 대한 검증은 아니다.

매 회차는 다음을 모두 만족해야 PASS다.

- 새로운 main/pthread/별도 group PID와 성공 marker가 순서대로 관측됨.
- 이전 세 PID가 모두 사라지고 Binary Manager가 `BINARY_INACTIVE`를 게시함.
- 동일 app heap descriptor의 registry 항목이 active/inactive 모두 제거되고 common heap-table slot이 0임.
- 이전 app RAM 안을 가리키는 delayed kernel/user free 및 semaphore-list 참조가 없음.
- native 관측값에서 heap 초기화와 해제가 각각 정확히 한 번 증가하고 해제한 heap이 이번 descriptor와 일치함. 해제 횟수는 **두 mutex destructor 성공 이후에만** 증가함.
- 모든 kernel heap의 `arena/uordblks/fordblks`가 warmup 정리 이후 baseline과 정확히 같음.

`BINMGR_RECOVERY=n`에서는 semaphore registry가 비어 있다는 사실만으로 mutex 파괴를 증명할 수 없다. 그래서 destructor 완료 이후의 별도 관측값을 함께 요구한다. 남긴 257 bytes의 회수는 앱 arena의 일괄 회수 증거다. 모든 종류의 커널 자원 누수가 없다는 보장은 아니다.

초기 group이 종료해도 같은 app ID 또는 heap을 가진 TCB가 남아 있으면 collector는 image를 회수하지 않는다. PID 해제 직후 아직 수행 중인 TCB/stack 정리도 IRQ/scheduling 경계로 보호한다. heap을 먼저 파괴하고 section RAM을 한 번만 반환한 뒤 재로드를 허용한다.

## 4. 소스·산출물·로그 증거

| 구성 | firmware build source | lifecycle runner source | firmware ELF SHA-256 |
| --- | --- | --- | --- |
| loadable_all | `5363bc64406ac25a7209b0462cba90b04736b1cf` | `14c3ca55872b2fa02a08988276402b2ca649a003` | `d526bc11a6d1b7ce4489f0d76e42f6004f0cf84c79689c5388801ade1b1ac48d` |
| loadable_apps | `14c3ca55872b2fa02a08988276402b2ca649a003` | 동일 | `a7c0b34565d00bfb0f91059204b7c641d8717154e8f7991a6138002dda10e032` |

빌드·lifecycle 실행 시작 시 Git 상태는 모두 clean이었다. `loadable_all` 빌드 이후의 `14c3ca558`은 host Python runner와 그 테스트만 변경하며 firmware 소스는 동일하다. 두 구성의 lifecycle/full suite 사이와 실행 후 패키지·설정 hash를 대조했다. 이후 변경은 문서뿐이다.

커밋한 [검증 receipt](validation/nuttx-mm-12.13-loadable-final.json)에 config·모든 package·로그 SHA-256, 회차별 PID/heap/정리 결과, 전체 suite 결과를 보관한다. 큰 바이너리·serial 로그는 Git에 넣지 않았으며 로컬 `build/nuttx-mm-validation/` 아래에 있다.

- `loadable-all-final-build/`, `loadable-apps-final-build/`: config, ELF/map, common/app packages, build log, provenance.
- `loadable-all-lifecycle-verified/`, `loadable-apps-lifecycle-verified/`: fresh RAM state, serial log, result receipt.
- `loadable-all-final-kernel-tc.log/json`, `loadable-apps-final-kernel-tc.log/json`: 별도의 fresh boot에서 실행한 전체 회귀.

시험 준비 중 TASH help 이름 잘림, warmup marker 중복, loader 출력과 입력 echo의 교차를 발견했다. 짧은 명령명, 회차 시작 marker, 모든 boot 완료 marker 수신 조건으로 해결했다. 실패한 probe와 timeout 로그는 로컬에 보존하고 위 최종 PASS와 구분했다.

## 5. host 검사와 남은 한계

| 검사 | 결과와 범위 |
| --- | --- |
| 실제 upstream allocator host test | ASan+UBSan PASS. realloc/data/alignment/overflow/OOM, 동시 할당, multiregion/extend, corruption, 지연 free 검사. |
| 실제 production pending head | 4 producers + 1 consumer, **80,000 nodes exactly once** PASS. arbitrary reclamation/IRQ ordering을 시뮬레이션했다는 의미는 아님. |
| registry/ioctl 계약 | **30/30 PASS**: ownership 5, ioctl wrapper 4, optional registry 6, capabilities 15. |
| lifecycle runner의 잘못된 증거 거부 | **11/11 PASS**: 부팅 순서/미완결행, stale·duplicate·PID/order 불일치, 실패·정리 누락 등. |
| native ARM compile/disassembly | 두 config에서 public/private kernel-user ABI 크기·offset 일치, user의 IRQ-mask 명령 없음, pending 연산의 외부 libatomic helper 없음. |
| Binary Manager 없는 exec host 검사 | Docker ARM64 `exec-no-bm` PASS. |
| board 전체 host `make check` | **FAIL(exit 2)**. 변경하지 않은 `qemu_armv8m_boot.c:141,299`의 64-bit host `int → size_t` 변환이 `-Werror=sign-conversion`에 걸려 loader compile matrix 중단. 이번 ARM firmware build 및 위 exec 검사의 실패와 구분한다. |

실행 중 앱의 강제 unload/update, owner가 중단된 backend mutex 복구, `BINMGR_RECOVERY`, retained-arena 최적화 reload, XIP 앱, SMP, 실제 IRQ 경쟁과 물리 보드는 검증하지 않았다. loadable+C++ constructor/destructor 조합은 정리 worker의 app-context provider가 없어 명시적으로 빌드 거부한다.

`HEAPINFO_USER_GROUP`는 이번 구성에서 껐다. 앱이 남긴 live allocation을 whole-heap teardown할 때 전역 group 누적값을 차감하는 처리는 아직 없어 이 통계를 unload/누수 판정에 쓰면 안 된다. kernel `mallinfo`의 물리 회수 검사는 이 제한과 별개다.

compat에는 native mutex·취소/errno/IRQ/task 전환 provider, imported object 전용 header/symbol map, backward realloc의 overlap-safe copy와 extend accounting 보정이 남아 있다. 이번에 추가한 protected pending head의 원자 연산도 kernel/user 공유 지연 free 계약을 대체할 때 함께 이관해야 한다. Tizen heap 선택·진단 metadata·앱 수명 정책은 영구 제품 계층이며 일괄 삭제할 임시 코드가 아니다.

## 6. 재현

`os`에서 `/opt/homebrew/bin/bash dbuild.sh menu`를 실행하고 **5. Clean Build and Re-Configure → qemu-armv8m → loadable_all → 1. Build** 순서로 선택한다. `loadable_apps`도 같은 절차로 clean build한다. 프로젝트의 [Mac QEMU runbook](AI/Mac_QEMU_ARMv8M_TASH_KernelTC.md)을 따른다.

각 build 직후 저장소 root에서 실행한다. `--output`은 새 디렉터리여야 한다.

```bash
os/mm/compat/tests/run.sh
python3 tools/nuttx-sync/verify.py --upstream /path/to/nuttx
/opt/homebrew/bin/python3.11 -m unittest discover \
  -s .github/scripts/tests -p 'test_qemu_armv8m_mm_lifecycle.py' -v
/opt/homebrew/bin/python3.11 .github/scripts/qemu-armv8m-mm-lifecycle.py \
  --config loadable_all --output build/nuttx-mm-validation/new-all --timeout 240
/opt/homebrew/bin/python3.11 .github/scripts/qemu-armv8m-kernel-tc.py \
  --config loadable_all --timeout 1200 \
  --log build/nuttx-mm-validation/new-all-kernel.log \
  --result build/nuttx-mm-validation/new-all-kernel.json
```

다음 clean build 후 두 runner의 `--config`를 `loadable_apps`로 바꾸고 출력 경로도 새로 지정한다. TASH 수동 명령 및 fixture/driver 조건은 [lifecycle 예제 문서](../apps/examples/nuttx_mm_test/LIFECYCLE.md)에 있다.
