# `origin/master`부터 QEMU ARMv8-M 네 recipe까지 변경 이력

이 문서는 현재 브랜치의 작업을 사람이 검토할 수 있도록 기준선부터
정리한 변경 원장이다. 소스 트리는 기존 `2f5a6936b`와 동일하며, 기능
커밋은 주제 경계가 명확한 18개 논리 커밋으로 정리했다. 각 논리 커밋의
commit message에는 해당 커밋의 실제 파일별 상태와 소유 계층별 수정
이유가 들어 있다.

## 범위와 기준선

| 기준 | SHA | 의미 |
| --- | --- | --- |
| `origin/master` | `149e1628f` | QEMU 작업이 시작된 upstream/master 기준 |
| `origin/main` | `85056ad92` | ARM64 Docker 문서까지 포함한 작업 기준선 |
| 정리 전 tip | `2f5a6936b` | 네 recipe 네트워크 문서까지 포함한 기존 tip |

`origin/master..origin/main`의 5개 커밋은 사용자가 지정한 “main 이전은
신경쓰지 않음” 범위에 따라 변경하지 않고 기준선으로 보존했다. 다만
전체 변경 이력을 재현할 수 있도록 아래에 함께 기록한다.

## main 이전 기준선 5개 커밋

| 원본 커밋 | 변경 내용 |
| --- | --- |
| `36affa38a` `tools/docker: Add TizenRT 2.0.1 ARM64 build image` | `tools/docker/tizenrt-2.0.1-arm64/`에 Apple Silicon용 Dockerfile과 사용 문서를 추가해 ARM64 cross-build 환경을 고정했다. |
| `72292507d` `build: Use the local ARM64 image for Docker builds` | 빌드 진입점이 registry image 대신 로컬 ARM64 image를 사용할 수 있도록 연결했다. |
| `2606acfcf` `build: Preserve the GNU Make jobserver in submakes` | submake에 jobserver 전달을 유지해 병렬 빌드 자원 제어를 보존했다. |
| `03b7542e0` `build/configs: Fix qemu and bk7239n host build paths` | QEMU와 BK7239N host build path를 현재 checkout 구조에 맞췄다. |
| `85056ad92` `docs: Document the Apple Silicon ARM64 Docker workflow` | root 문서에서 ARM64 runbook을 연결하고 Bash/Docker credential/image/platform 검증과 보안 경계를 문서화했다. |

## main 이후 정리된 18개 논리 커밋

아래의 SHA는 히스토리 정리 후 SHA다. 괄호 안은 정리 전 원본 커밋
범위이며, 파일 목록은 각 commit message의 파일별 목록과 함께 읽는다.

### 1. GCC long-double ABI 일반화

- 정리 SHA: `690d31f777`
- 원본: `c3d6df659`
- 파일: `tc_libc_math.c`, `lib_cbrtl.c`, `lib_exp2l.c`, `lib_truncl.c`,
  `os/include/complex.h`
- 변경: target 이름을 추측하지 않고 `long double` ABI의 mantissa/exponent
  특성을 기준으로 math helper를 선택한다. cbrt/exp2/trunc testcase에는
  infinity, NaN, signed zero, exact result를 추가했다.

### 2. cross-toolchain loadable packaging

- 정리 SHA: `9116ded364`
- 원본: `90c6e4fad`
- 파일: `os/Makefile.unix`, `os/dbuild.sh`,
  `os/tools/{loadable_binary_header.py,mkbinheader.py,mkchecksum.py,mksyscall.c}`
  및 `os/tools/tests/*`
- 변경: package header/checksum/syscall 생성에서 host integer layout과
  toolchain default 의존성을 제거하고 fixed little-endian binary contract를
  사용한다. Python/C tool round-trip 및 CRC/header 회귀 테스트를 추가했다.

### 3. Binary Manager 없이 app-separated loadable 실행

- 정리 SHA: `c065b9db6`
- 원본: `c08931a30`
- 파일: `os/binfmt/binfmt_execmodule.c`, `os/binfmt/libelf/libelf_sections.c`,
  `os/binfmt/libxipelf/xipelf.c`, `os/include/tinyara/{binary_manager.h,binfmt/binfmt.h}`,
  `os/kernel/init/os_bringup.c`
- 변경: ELF/XIP execution path에서 owner/name/app-id와 loadable header를
  전달한다. Binary Manager를 사용하는 기존 ABI는 유지하고, app-separated
  QEMU loadable은 Binary Manager 없이도 metadata를 잃지 않도록 했다.

### 4. child lifecycle, SMP signal, mqueue testcase 안정화

- 정리 SHA: `a01d53b877`
- 원본: `552ece058`, `d48b5699a`, `8e97a5119`
- 파일: `sched_waitid.c`, `sched_waitpid.c`, `task_reparent.c`,
  `task_setup.c`, `group_signal.c`, `tc_sched.c`,
  `tc_memory_safety.c`, kernel testcase `Kconfig`
- 변경: child status를 waitpid가 reap할 때까지 보존하고 reparent 경로에서
  status를 잃지 않게 했다. SMP group signal build를 고치고, mqueue receiver를
  SIGUSR1로 깨우며 EINTR 재시도는 실제 수신 message만 세도록 했다.

### 5. QEMU ARMv8-M architecture, loader, recipe layout

- 정리 SHA: `e1d40cfa6`
- 원본: `7af23168c`, `406f14a1e`, `72ce15532`, `5f747c985`
- 파일 계층: `os/arch/arm/{Kconfig,include/qemu-armv8m,src/qemu-armv8m}`와
  `os/board/qemu-armv8m/{include,src,tests}`, `os/Kconfig`,
  `build/configs/qemu-armv8m/**`, `.github/scripts/tests/qemu_armv8m_layout.py`
- 변경: Cortex-M33 MPS2-AN505의 IRQ/timer/serial/ELF/startup을 추가하고,
  board boot에서 loadable header·ELF placement·package 오류를 검증한다.
  flat, `loadable_all`, `loadable_apps`, `xip_all`의 linker/heap/package
  layout을 정의하고 host-side layout test로 경계를 검사한다.

### 6. registry-backed `os_api_test`와 shared kernel wrapper

- 정리 SHA: `aa23a41172`
- 원본: `bc58f10db`, `057a35ee9`, `ac3460572`, `df39bf508`,
  `241feb3db`, `ae9271fb8`, `81ba52e25`
- 파일 계층: `.github/scripts/tests/os_api_test*`와 pbuf/kernel wrapper
  fixtures, `apps/examples/testcase/le_tc/{kernel,network}`,
  `os/drivers/os_api_test/**`, `os/include/tinyara/os_api_test_drv.h`,
  task start-hook test
- 변경: command registry와 public/prototype/driver/provider topology를
  하나의 계약으로 맞췄다. pbuf, task, scheduler, signal, clock, timer,
  semaphore, pthread, IRQ, group, workqueue, KMM, mqueue, VFS, procfs,
  pipe, termios, watchdog, PM, RTC, reboot reason, binfmt, Binary Manager,
  log dump, memory leak provider를 capability predicate와 함께 추가했다.
  `kernel_tc_main`이 소유한 descriptor를 optional wrapper가 공유하도록
  바꾸고 success/failure host harness를 추가했다.

### 7. QEMU kernel-test runner와 CI workflow

- 정리 SHA: `43770a916d`
- 원본: `20630577a`, `3087e973f`
- 파일: `.github/scripts/qemu-armv8m-kernel-tc.py`,
  `qemu_armv8m_{prompt,protocol}.py`, runner/layout/adversarial tests,
  `.github/workflows/qemu-armv8m.yml`, workflow verifier
- 변경: serial prompt epoch과 protocol을 파싱하는 runner를 만들고,
  전체 serial log에서 성공 marker가 정확히 나오는지 확인한다. negative
  package mutation, timeout, assertion, exact rejection을 검증하는 pinned
  workflow와 executable contract test를 추가했다.

### 8. QEMU target role 문서

- 정리 SHA: `5bf59e3a67`
- 원본: `6bc776670`
- 파일: `build/configs/qemu-armv8m/{README.md,READMD_KOR.md}`,
  `build/configs/qemu-targets.md`, `build/configs/qemu/README.md`,
  `.github/scripts/tests/test_qemu_armv8m_docs.py`
- 변경: hello/loadable/XIP/kernel-test의 역할과 명령을 영문·국문 문서로
  정리하고 target index에서 연결했다. 문서 링크와 명령 일관성을 자동 검사한다.

### 9. menu build와 Docker platform 자동 선택

- 정리 SHA: `67f11179eb`
- 원본: `b6efd646d`
- 파일: `AGENTS.md`, `os/dbuild.sh`, `docs/AI/*`,
  `docs/Human/QEMU_ARMv8M_Terminal_Guide.md`
- 변경: 사람이 `./dbuild.sh menu`에서 `Clean Build and Re-Configure`와
  build option을 선택하는 절차를 기준으로 문서화했다. `dbuild.sh`가 Docker
  server architecture를 감지해 `linux/arm64` 등 platform을 자동 전달하고,
  exFAT bind-mount의 알려진 copy-race marker만 한 번 재시도하도록 했다.

### 10. SRAM multiheap과 persistent A/B runtime

- 정리 SHA: `703b1efaa8`
- 원본: `93fc10fdb`
- 파일 계층: `.github/scripts/qemu_armv8m_ab.py`와 runner/protocol/layout/
  workflow tests, `build/configs/qemu-armv8m/**`, `loadable_apps/**`,
  `os/board/qemu-armv8m/**`, `os/fs/driver/mtd/rammtd/**`,
  `os/tools/{mkbootparam.py,mkldscript.py}`, `os/Kconfig`
- 변경: QEMU RAM을 main RAM, loaded-app heap, upper SRAM heap으로 나누고
  `RAM_KREGIONx_HEAP_INDEX=0,2,1` 계약을 도입했다. RAM-backed flash와
  kernel/common/app package partition을 Binary Manager에 등록하고,
  bootparam recovery·dual XIP slot·persistent A/B state를 runner에 연결했다.
  slot 재시도는 guest가 clean reboot 후 active slot을 바꾼 경우로 제한했다.

### 11. SRAM과 A/B 운영 문서

- 정리 SHA: `84709d01eb`
- 원본: `a7d835c46`
- 파일: QEMU README/국문 README/target index, AI runbook·TASH guide·board
  guide·Human terminal guide
- 변경: 각 heap의 주소/소유권, package/A-B 경계, menu build와 runner 사용법,
  clean-build·boot smoke·full testcase·hardware QA의 증거 범위를 분리해 기록했다.

### 12. semaphore holder와 multiheap usage 안정화

- 정리 SHA: `137cfe4128`
- 원본: `0f2cc5740`, `cb6b9f808`
- 파일 계층: `os/kernel/semaphore/**`, `os/kernel/binary_manager/**`,
  `os/mm/mm_heap/mm_getsize.c`, `os/binfmt/binfmt_execmodule.c`, 생성 config/
  syscall 경계, semaphore kernel testcase와 QEMU hello defconfig
- 변경: holder release와 waiter handoff에서 count/PI/비-holder/interrupt/
  task-exit 경로를 일관되게 갱신하고 finite holder pool을 연결했다. flat
  multiheap의 free/largest-node를 모든 heap에서 합산하고 loadable app peak
  accounting에서 reset된 stack을 이중 차감하지 않도록 했다.

### 13. Docker bind-mount copy race 재시도

- 정리 SHA: `7601e8fe66`
- 원본: `e38e71318`
- 파일: `os/dbuild.sh`, `.github/scripts/tests/test_qemu_armv8m_dbuild_retry.py`
- 변경: 일반 build만 exFAT copy marker가 있을 때 정확히 한 번 재시도하고,
  두 시도의 log와 두 번째 exit status를 보존한다. clean/other make target,
  compiler error와 mixed marker를 fake Docker harness로 구분한다.

### 14. kernel assertion fail-fast

- 정리 SHA: `636597b1d1`
- 원본: `14219c750`
- 파일: `.github/scripts/qemu_armv8m_protocol.py`,
  `.github/scripts/tests/test_qemu_armv8m_kernel_tc.py`
- 변경: assertion marker가 serial read 경계를 가로질러도 누적 window에서
  발견하고, package rejection이나 timeout보다 assertion을 먼저 보고한다.
  producer/consumer handshake test로 이 precedence를 고정했다.

### 15. 검증된 kernel-test workflow 문서

- 정리 SHA: `6abc8fd971`
- 원본: `8053e4ed9`
- 파일: AI/Human/QEMU README 및 `test_qemu_armv8m_docs.py`
- 변경: 네 recipe clean build/runtime receipt, holder-pool 역할, assertion
  실패 의미, bounded Docker retry와 runner 사용법을 갱신했다. make-download의
  오래된 troubleshooting을 config-aware Python runner 설명으로 교체했다.

### 16. TASH, SmartFS, MMINFO와 defconfig 비교

- 정리 SHA: `9ee9cbf082`
- 원본: `09103fdaa`, `7f5a2e9af`, `d3b4fc424`
- 파일: QEMU hello/전체 네 recipe defconfig, QEMU boot source, AI/Human
  가이드와 기존 defconfig comparison 문서
- 변경: BK7239N 기준으로 allocator priority, assert reset, TASH, SmartFS,
  RAMMTD partition과 auto-mount를 맞추되 QEMU `CONFIG_ENABLE_PS`는 유지했다.
  네 recipe의 normalized/physical BK-vs-QEMU 비교를 추가하고, `heapinfo`가
  `/dev/mminfo`를 실제로 사용할 수 있도록 네 defconfig에 `CONFIG_MMINFO=y`를 켰다.

### 17. LAN9118 Ethernet runtime

- 정리 SHA: `a3dc62131a`
- 원본: `41833f4d8`
- 파일: `os/board/qemu-armv8m/src/qemu_armv8m_lan9118.c`, board Make/IRQ/chip,
  `os/net/{lwip,netmgr}/**`, `lib/libc/netdb/lib_gethostbyname.c`,
  `apps/system/utils/netcmd.c`, QEMU Make.defs
- 변경: MPS2-AN505 LAN9118에 bounded RX/TX, interrupt-safe worker, enable/
  disable lifecycle을 추가했다. Wi-Fi LWNL control plane 없이 Ethernet
  netmgr를 초기화하고 MAC/MTU와 lwIP resolver를 연결하며, partial init rollback
  시 NIC/ops state가 누수되지 않도록 했다.

### 18. 네트워크 config, runtime protocol, 문서

- 정리 SHA: `d63eb1d9e3`
- 원본: `723326f6f`, `39a72ff6d`, `2f5a6936b`
- 파일: 네 QEMU defconfig, `qemu-armv8m-kernel-tc.py`,
  `qemu_armv8m_protocol.py`, network protocol/defconfig tests, QEMU README,
  AI/Human 문서
- 변경: 네 recipe에 IPv4/IPv6, DHCP, DNS, TCP/UDP/raw/multicast와 TASH
  network command를 켰다. runner는 user-mode NIC에서 ifdown/ifup, DHCP,
  local gateway, DNS, statistics, `network_tc`를 먼저 실행한 뒤 `kernel_tc`를
  실행하고, public ICMP는 diagnostic-only로 분리한다. 문서에는 `Network TC
  End [PASS : 161, FAIL : 0]`, 네 recipe kernel_tc receipt와 Wi-Fi/BLE가
  QEMU Ethernet 검증 범위가 아니라는 경계를 기록했다.

## 마지막 Human 문서 커밋

기능 변경 18개를 정리한 뒤 현재 `HEAD`의 마지막 별도 커밋에서 다음 두
문서만 추가했다.

- `docs/Human/BK7239N_QEMU_ARMv8M_4Recipe_Defconfig_Comparison.md`: 네
  recipe의 BK7239N/QEMU 심볼·메모리·partition·loader 차이와 line-by-line
  비교 방법
- `docs/Human/OriginMaster_to_QEMU_ARMv8M_Change_History.md`: 이 원장과
  기준선·논리 커밋·파일 계층별 변경 이유·검증 경계

## 검증 및 남은 경계

히스토리 정리 자체에 대해서는 다음을 확인했다.

- 정리 전 tip과 정리 후 기능 tip의 tree diff가 없다.
- 기능 이력은 기준선 5개와 논리 커밋 18개로 구성되고, 현재 `HEAD`에는
  위의 마지막 Human 문서 커밋이 추가되어 있다.
- 작업 트리는 문서 작성 전까지 clean이었다.

기존 기능 검증 receipt는 소스에 남아 있는 문서 기준으로 다음과 같다.

- 네 QEMU recipe clean build와 runner smoke
- `network_tc`: `PASS : 161, FAIL : 0`
- `hello` `kernel_tc`: `PASS : 459, FAIL : 0`
- `loadable_all`, `loadable_apps`, `xip_all` `kernel_tc`: 각각
  `PASS : 447, FAIL : 0`
- package corruption, omitted package, alternate-slot recovery와 SmartFS
  검증은 QEMU software path의 결과다.

실제 BK7239N/RTL 보드의 Wi-Fi, BLE, camera, NPU, vendor flash/PSRAM 동작은
이 QEMU receipt로 대체되지 않는다. 관련 hardware QA는 각 보드의 실제
peripheral과 flashing/bootloader 경로에서 별도로 수행해야 한다.
