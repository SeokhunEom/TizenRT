# QEMU ARMv8-M fault 정지 현상 재현

2026-10-09 18:49:05–18:49:11 KST에 `mps2-an505`에서 세 현상을 각각 세 번
재현했다. 9회 모두 명시한 관측 조건을 만족했다. 여기서 `pass`는
의도한 정지 현상과 증거를 확인했다는 뜻이다.

최초 실행 당시 브랜치는 `codex/qemu-armv8m-fault-lab`, worktree는
`/Users/seokhun/Dev/TizenRT/codex/qemu-armv8m-fault-lab`이다.
기준선은 기존 `codex/qemu-armv8m-kernel-tc`의 커밋
`5c0120f685ff47ab17f7ec26c0554965e312bc27`이다.

현재 통합 테스트는 [중첩 IRQ 패치 비교 문서](QEMU_ARMv8M_Nested_IRQ_Patch_Comparison.md)의
`codex/qemu-armv8m-fault-tests` 브랜치를 사용한다. 이 문서의 관측은 최초
패치 포함 실행의 역사적 결과다. 통합 브랜치의 기본 checkout은 패치 제거
상태이므로 현재 소스의 패치 상태와 혼동하지 않는다.

## 결과

모든 실험은 동일한 펌웨어를 새 QEMU 인스턴스에 부팅하고 TASH 명령으로
실제 `UDF` 명령을 실행한다. 먼저 `up_usagefault()` 진입과
`CFSR.UNDEFINSTR=1`을 확인한 후 각 정지 경로를 관측한다.

| 명령 | 주입 또는 설정 | 실제 관측 | 반복 결과 |
| --- | --- | --- | --- |
| `faultlab panic` | assert 자동 재부팅 비활성화, system halt 활성화 | PC `0x10000cf6`, `up_assert+0x48e`, `b .` 무한 루프 | 3/3 |
| `faultlab lockup` | UsageFault 진단 출력 후 MSP=`0x60000000`, 추가 `UDF` | PC `0x1000137e`, `exception_common+0x2e`; QEMU lockup 보고 | 3/3 |
| `faultlab uart` | UsageFault 진단 출력 후 UART TX 비활성화, 송신 버퍼 채우기 | PC `0x1000043c`, `qemu_armv8m_lowputc+0x4`; `TXFULL=1`, `TXEN=0` | 3/3 |

panic과 UART는 게스트 실행을 재개한 뒤 0.3초 간격으로 세 번 중단해
레지스터를 읽었다. 모든 표본에서 위 PC가 유지됐다. 단순 timeout이나
디버거 breakpoint 정지를 재현 성공으로 취급하지 않는다.

### panic 정지

실제 UsageFault 진단과 `PANIC()` → `up_assert()` 경로를 통과했다.
`_up_assert()`의 정지 정책이 최적화로 `up_assert()` 내부에 인라인됐다.

```text
10000cf6: e7fe  b.n 10000cf6 <up_assert+0x48e>
CFSR = 0x00010000
HFSR = 0x00000000
```

이 정지는 설정에 따른 panic 정책이다. CPU는 무한 루프를 실행하며,
추가 HardFault는 관측되지 않았다.

### 잘못된 MSP와 추가 fault로 인한 lockup

UsageFault 진단 출력 뒤 테스트 코드가 MSP를 명시적으로 잘못된 주소로
바꾸고 두 번째 `UDF`를 실행했다. QEMU 예외 추적 순서는 다음과 같다.

```text
Taking exception 1 [Undefined Instruction]
...BusFault with BFSR.STKERR
...taking pending secure exception 3
Taking exception 4 [Data Abort]
...at fault address 0x5fffffdc
...with CFSR.PRECISERR and BFAR 0x5fffffdc
qemu: fatal: Lockup: can't escalate 3 to HardFault (current priority -1)
```

lockup 시점의 QEMU 덤프는 SP=`0x5fffffe0`, LR=`0xfffffff1`,
PC=`0x1000137e`, IPSR=3을 보였다. 실패한 명령은 다음과 같다.

```text
1000137e: f84d 1d04  str.w r1, [sp, #-4]!
```

이미 HardFault를 처리하는 문맥에서 공통 예외 진입 코드가 잘못된 MSP에
레지스터를 저장하다 다시 fault가 발생했다. C 함수 `up_hardfault()`에는
도달하지 못했다. QEMU는 이 CPU lockup을 fatal로 보고하고 프로세스를
SIGABRT로 종료했다. 실제 칩의 watchdog/reset 동작은 이 결과에 포함되지 않는다.

lockup 후에는 QEMU가 종료되어 CFSR/HFSR을 디버거로 다시 읽지 못했다.
최종 stacking/precise BusFault는 QEMU의 예외 추적에서 확인한 내용이다.

### UART 출력 대기

UART0의 `CTRL.TXEN`을 해제한 뒤 DATA에 문자를 써 버퍼를 채웠다.
다음 `lldbg()`가 실제 polling driver에서 멈췄다.

```text
UART0 STATE = 0x00000001  (TXFULL=1)
UART0 CTRL  = 0x0000002a  (TXEN=0)
CFSR = 0x00010000
HFSR = 0x00000000
1000043a: 685a  ldr  r2, [r3, #4]
1000043c: 07d2  lsls r2, r2, #31
1000043e: d4fc  bmi.n 1000043a
```

추가 HardFault 없이 UART 상태를 계속 읽는 경우다. 이 실험은 UART polling
대기를 재현했으며 semaphore/spinlock 교착은 별도로 주입하지 않았다.

## 역사적 수정과의 관계

세 결과는 **명시적 fault 주입으로 정지 형태를 재현한 실험**이다.
`4ee6ceb9`의 이전 PSP 저장·복원 및 보조 IRQ 스택 수정은 그대로 포함되어
있으며, `up_exception.S`를 수정하거나 패치를 제거하지 않았다.

이 recipe는 `hello` 기반 flat 빌드다. 관측 시 PSP=0, 원래 예외의
EXC_RETURN=`0xfffffff9`이고, `CONFIG_ARCH_NESTED_INTERRUPT`는 활성화되지
않았다. 따라서 이 결과만으로 역사적 PSP 복귀 경계의 경쟁을 재현했다거나,
패치 제거가 이번 lockup의 원인이라고 결론 내릴 수 없다.

그 인과관계를 검증하려면 별도 protected/PSP 구성에서 실제 예외
진입·복귀 경계에 중첩 IRQ를 유발하고, 수정 전후를 비교해야 한다.

## 다시 실행하기

최초 실행 recipe의 [기존 빌드 가이드](../AI/Mac_QEMU_ARMv8M_TASH_KernelTC.md)를
따른다. Homebrew Bash, QEMU, Docker ARM64 이미지가 준비되어 있다.

새로 빌드하려면 메뉴에서 `qemu-armv8m` → `fault_lab` → `1. Build with
Current Configuration`을 선택한다. 다른 recipe로 이미 설정되어 있으면
`5. Clean Build and Re-Configure`부터 진행한다.

```bash
cd /Users/seokhun/Dev/TizenRT/codex/qemu-armv8m-fault-lab/os
/opt/homebrew/bin/bash ./dbuild.sh menu
```

메뉴 입력은 터미널에서 진행한다. 미리 모든 선택을 파이프로 보내면
하위 빌드 명령이 입력을 소비해 메뉴가 EOF에서 반복될 수 있다.

이미 빌드된 펌웨어로 전체 관측을 다시 실행할 수 있다.
`--output`에는 아직 존재하지 않는 경로를 지정한다.

```bash
cd /Users/seokhun/Dev/TizenRT/codex/qemu-armv8m-fault-lab
python3 tools/qemu-fault-lab/run.py \
  --case all --repeat 3 \
  --output build/qemu-fault-lab/my-run
```

한 가지 현상만 실행하려면 `--case panic`, `--case lockup`, `--case uart`를
사용한다. 수동 TASH 실행 시에도 같은 `faultlab` 명령을 사용하고 각
실험 후 새 게스트를 부팅한다.

## 증거 위치와 무결성

최종 증거는 [`build/qemu-fault-lab/evidence-20261009`](../../build/qemu-fault-lab/evidence-20261009)에 있다.

- [`results.json`](../../build/qemu-fault-lab/evidence-20261009/results.json): 9회 결과, 관측 시각, 레지스터, 코드 및 이미지 해시
- 각 `panic-1`/`lockup-1`/`uart-1` 등의 디렉터리: `serial.log`, `exceptions.log`, `stderr.log`, `rsp.log`, `result.json`
- `tinyara`, `effective.config`, `sources/`, `build.log`, `disassembly.txt`: 관측한 ELF 및 정확한 테스트 입력

QEMU 버전은 `11.1.2`, 빌드 compiler는 Docker 이미지의 Arm GCC
`10.3-2021.10`이다. 원시 로그와 ELF는 Git ignore된 worktree 내부에 보존했다.

```text
ELF SHA-256:
d0f66936e369f452765944a083b0d75487658eff2719f4a86a770c8a3c71e002
effective .config SHA-256:
814788da3e91a41a53218ac53a8fb1d8cca520d839c8804061102b6b5c5331f2
```

테스트용 코드와 hook은 `CONFIG_QEMU_FAULT_LAB=y`일 때만 빌드된다.
일반 `hello` 및 loadable recipe의 설정은 바꾸지 않았다.
