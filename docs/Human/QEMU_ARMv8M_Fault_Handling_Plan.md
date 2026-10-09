# ARMv8-M fault handling 개선 계획

작성일: 2026-10-09. 검토 기준: `codex/qemu-armv8m-fault-tests`의 `68f42f203`.
사용자가 선택한 운영 정책은 **진단 정보를 보존한 뒤 자동 재부팅**이다.
이 문서는 **구현 전 계획 원본**이다. 아래의 ‘현재/예정’은 `68f42f203` 시점이다.
이후 사용자의 지시에 따라 같은 브랜치에서 패치 복원과 구현을 별도 커밋으로 진행했다.
적용 범위·완료된 검증·남은 하드웨어 항목은
[구현 결과](QEMU_ARMv8M_Fault_Recovery_Results.md)를 기준으로 한다.
계획에 적힌 모든 보드/FPU/SMP/독립 watchdog 항목이 구현됐다는 뜻은 아니다.

## 1. 목표와 성공 기준

정상 중첩 IRQ는 fault 없이 처리·복귀시키고, 실제 fatal fault는 최소 기록을 먼저
보존한 뒤 제한 시간 안에 reset을 요청한다. 정상 경로로 돌아갈 근거가 없는 UDF나
손상된 예외 프레임에 대해 PC만 증가시켜 실행을 계속하지 않는다.

현재 실험의 [결과](QEMU_ARMv8M_Nested_IRQ_Patch_Comparison.md)와
[통합 재검증 JSON](../evidence/qemu-fault-lab/20261009/consolidation-validation.json)을
기준선으로 사용한다. 기존 `pass`는 정지 현상 재현 성공이므로 개선의 성공 기준으로
재사용하지 않는다.

| 번호 | 현재 관측: 패치 제거 / 포함 | 개선 후 목표 | 필요한 변경 |
| --- | --- | --- | --- |
| 1 `early40` | panic / 정상 IRQ 후 테스트 spin | 같은 주입 시점에서 두 IRQ 처리, 올바른 Thread 복귀, 스케줄러 계속 진행 | 2022년 수정 복원, 진입·복귀 중 PSP/MSP와 IRQ 상태의 일관성 보장 |
| 2 `early00` | panic / panic | priority `0x00`을 유지한 채 정상 처리·복귀. 해당 IRQ 계약을 지원하지 못한다면 미해결로 표시 | BASEPRI 한계, 짧은 PRIMASK 보호 구간, IRQ 진입/복귀 및 OS 호출 계약 검토 |
| 3 `late40` | 정상 IRQ 후 테스트 spin / 동일 | 기존 정상 동작 유지, 실제 Thread/TASH 및 다른 task 진행 확인 | 중첩 깊이·context switch·IRQ stack 회귀 검사 |
| 4 UDF | UsageFault 뒤 panic 무한 루프 / 동일 | 첫 fault의 최소 기록 보존 → software reset → 다음 부팅에서 기록 확인 | fatal capture와 reset을 긴 assert 진단보다 앞에 배치 |
| 5 잘못된 MSP + 2차 UDF | HardFault 진입 중 다시 fault, QEMU fatal lockup / 동일 | lockup 이전 HardFault 진입이 가능하면 비상 경로로 기록·reset. 이미 lockup이면 독립 HW watchdog으로 reset | 스택을 사용하지 않는 HardFault 진입부, 비상 스택, 재진입 처리, HW watchdog |
| 6 UART TX 중단 | TXFULL polling 무한 대기 / 동일 | UART 실패를 기록하고 출력을 포기한 뒤 reset, 다음 부팅에서 기록 확인 | 출력·flush 전체의 상한, UART에 의존하지 않는 진단 보존 |

현재 1–3번의 `qemu_fault_lab_nested_spin`은 테스트가 만든 루프다. 이 위치에
도달한 것과 정상 RTOS 작업이 계속되는 것은 구분해야 한다. 개선 하네스에서는
정상 context로 돌아오는 test task/trampoline과 별도 heartbeat task로 확인한다.

5번은 두 단계로 나눈다. **HardFault 첫 명령을 실행할 수 있는 손상 스택**은
소프트웨어 개선 대상이다. **이미 architectural lockup인 코어**는 일반 C handler로
회복할 수 있다는 보장을 하지 않는다. Arm M33 가이드 §2.5.4와
[NuttX 조사 문서](QEMU_ARMv8M_Fault_Handling_NuttX_Reference.md)의 경계를 적용한다.

## 2. 코드 검토에서 확인한 문제와 아직 확인할 가설

| 근거 | 확인한 사실 | 계획에 미치는 영향 |
| --- | --- | --- |
| [up_exception.S](../../os/arch/arm/src/armv8-m/up_exception.S), 154–297행 | 현재 baseline은 패치 제거 상태이며, context 저장 전 MSP를 PSP로 바꾼다. 인터럽트 차단과 IRQ stack 전환은 뒤에 있다 | 패치 제거본을 최종 운영 코드로 삼지 않고 원본 수정을 먼저 복원한다 |
| [chip.h](../../os/arch/arm/include/qemu-armv8m/chip.h), 95–112행 | 현 구성의 disable threshold는 `0x20`; 테스트 2는 priority `0x00` | BASEPRI를 올리는 것만으로 2번을 차단할 수 없다 |
| [up_doirq.c](../../os/arch/arm/src/armv8-m/up_doirq.c), 100–132행 | `flags` 선언 후 첫 `irqrestore(flags)`까지 초기화가 없다 | 독립적으로 수정해야 하는 결함이다. 이 결함이 2번 INVSTATE의 유일한 원인인지는 아직 증명하지 않았다 |
| [up_vectors.c](../../os/arch/arm/src/armv8-m/up_vectors.c) | HardFault를 포함한 vector가 공통 진입부로 향한다 | 손상된 MSP에 첫 software save를 하기 전 분기하는 구조가 필요하다 |
| [up_usagefault.c](../../os/arch/arm/src/armv8-m/up_usagefault.c), [up_hardfault.c](../../os/arch/arm/src/armv8-m/up_hardfault.c) | 전달된 `regs`를 즉시 읽고 `lldbg`한 뒤 `PANIC()`을 호출한다 | 잘못된 frame 접근 및 UART 정지를 capture보다 먼저 일으킬 수 있다 |
| [up_assert.c](../../os/arch/arm/src/armv8-m/up_assert.c), 590–665행 | 첫 출력이 재진입 guard보다 앞이고, task/stack/heap 진단·crashdump·console flush 뒤 최종 reset 정책을 실행한다 | `AUTORESET=y` 하나만으로 5·6번을 해결하지 못한다 |
| [qemu_armv8m_serial.c](../../os/arch/arm/src/qemu-armv8m/qemu_armv8m_serial.c), 382–387행 | `TXFULL` 해제를 무기한 기다린다 | panic 출력은 실패·시간 초과를 허용해야 한다 |
| [up_flush_console.c](../../os/arch/arm/src/common/up_flush_console.c) | 직접 UART 출력으로 전송 버퍼를 비운다 | 문자당 timeout뿐 아니라 전체 flush의 byte/time 상한이 필요하다 |
| [crashdump.c](../../os/board/common/crashdump.c), 478행 및 562행 이후 | UART host handshake를 무기한 기다리고, `CONFIG_WATCHDOG`일 때 watchdog을 끈다 | 운영 fatal 경로에서 이 대화형 dump를 호출하지 않는다. reset 보장용 watchdog을 유지한다 |
| [up_systemreset.c](../../os/arch/arm/src/armv8-m/up_systemreset.c) | AIRCR `SYSRESETREQ`를 쓴 후 기다린다 | reset 요청 실패/권한/클럭 문제에 대비한 독립 HW watchdog이 필요하다 |

추가로 C 함수 호출 시 SP의 8-byte 정렬, xPSR stack-alignment 비트,
`EXC_RETURN`, `MSPLIM/PSPLIM`, `current_regs`와 `g_nestlevel`의 진입·복귀
불변식을 확인한다. 정적 검토 항목을 이미 재현된 원인으로 단정하지 않는다.

## 3. 구현 구조

### A. 정상 IRQ의 context 무결성

1. 기존 비교용 patch를 적용해 2022년 PSP 저장·복원 및 보조 IRQ stack 수정을
   복원하고, patch-present 기준선 1–6을 다시 기록한다.
2. `up_doirq()`의 interrupt-state 소유권을 명확히 한다. 초기화되지 않은
   `flags`를 복원하지 않도록 수정하고 진입 당시 mask를 정확히 보존한다.
3. 첫 진입부터 frame/current_regs/nestlevel이 안정될 때까지와 최종 복귀 구간을
   검토한다. 우선 후보는 기존 ABI를 유지하면서 **아주 짧은 PRIMASK 보호 구간**을
   두는 것이다. 원래 PRIMASK·BASEPRI를 보존하고, 안전한 IRQ stack에서만 중첩을
   다시 허용한다. NMI/HardFault는 별도 fatal 경로를 사용한다.
4. priority 0의 일반 IRQ를 지원하는 경로와, OS API를 사용할 수 없는 전용
   zero-latency 경로를 혼합하지 않는다. 테스트 2의 priority를 바꾸거나 주입 hook을
   옮겨 fault를 숨기면 통과로 인정하지 않는다. PRIMASK 구간에 pending된 IRQ가
   해제 후 정확히 한 번 처리되는 것은 정상 처리로 인정한다.
5. 위 변경으로 2번을 해결하지 못하면 MSP를 임시 context 포인터로 재사용하는
   설계를 없애는 대안을 검토한다. 현대 NuttX의 PendSV 구조 전체를 이식하는 것은
   범위가 훨씬 크므로 별도 결정으로 남긴다.

context 구조를 바꿀 때에는 `irq_cmnvector.h`뿐 아니라 `up_saveusercontext.S`,
`up_fullcontextrestore.S`, `up_initialstate.c`, signal 전달·복귀, SVC, debugger의
register 인덱스까지 함께 검사한다. 현 QEMU flat 성공으로 protected/FPU/SMP의
호환성을 주장하지 않는다.

### B. 스택 손상과 재진입을 견디는 최소 fatal 진입부

- HardFault vector에 stackless assembly veneer를 둔다. 손상된 MSP에 `push`,
  `stmdb`, C prologue를 수행하기 전에 원래 MSP/PSP/LR/CONTROL과 fault status를
  확보하고, 검증된 내부 RAM의 비상 stack으로 전환한다.
- 비상 stack 전환 시 MSPLIM과 정렬을 함께 처리한다. HardFault에서 다시 fault가
  나면 더 이상 회복 기회가 없을 수 있으므로 기존 stack·TCB·heap·UART를 읽지 않는다.
- 고정 SRAM의 per-core 진입 상태로 첫 fault와 재진입을 구분한다. 재진입은 긴 dump나
  lock 획득을 생략하고 첫 fault를 보존한 채 작은 보조 기록과 reset 경로로 간다.
- `CFSR`의 stacking/unstacking/lazy-FPU 오류, EXC_RETURN, frame 범위·정렬을
  검증한 뒤에만 stacked PC/LR 등을 복사한다. 검증 불가능한 필드는 `invalid`로
  표시하며 임의 주소를 probe하거나 `memcpy`하지 않는다.
- 일부 빌드에서 HardFault가 SVC를 대행하는 기존 경로를 먼저 확인한다. 정상 SVC
  동작을 무조건 fatal로 바꾸지 않으며, 지원 조건을 명시적으로 보존한다.
- UsageFault/BusFault/MemManage의 fatal 진단도 공통 capture/reset 정책으로 모은다.
  일반 assert와 기존 정상적인 앱 복구 기능은 명시적으로 분기해 회귀를 방지한다.

처음 구현할 spike는 기존 5번에서 **새 HardFault 첫 명령에 도달하는지**, 원래
MSP가 잘못돼도 비상 stack으로 옮길 수 있는지를 확인하는 것이다. 첫 명령 전에
hardware lockup이 발생하는 변형은 HW watchdog 검증 대상으로 남긴다.

### C. RAM에 먼저 남기고, 다음 부팅에서 출력

- linker에 전용 `NOLOAD`/`.noinit` 영역을 예약한다. `[ _sbss, _ebss )` 초기화,
  heap, DMA, bootloader 덮어쓰기 범위에서 제외하고 MAP으로 검증한다.
- 고정 크기 record에 version, sequence, build ID, fault type, CPU/security 상태,
  fault status register, 원래 SP/EXC_RETURN, 유효한 register, capture 단계,
  재진입·출력실패 표시를 넣는다. 예: 첫 fault와 보조 fault를 별도 slot에 보존한다.
- malloc, scheduler lock, semaphore, filesystem, flash erase/write, TCB 목록 순회 없이
  최소 record를 작성한다. commit marker/checksum을 마지막에 저장해 중간 reset과
  전원 차단으로 생긴 부분 기록을 검출한다. 첫 유효 기록을 재진입이 덮어쓰지 않는다.
- 필요한 memory barrier와 해당 SoC의 cache/retention 규칙을 반영한다. `.noinit`이라는
  이름만으로 reset 후 보존이 보장되는 것은 아니므로 실제 reset 종류별로 시험한다.
- 부팅 초기에 기록을 검증하고 RAM에 안전하게 확보한다. 정상 동작이 회복된 뒤 UART나
  저장장치로 내보내며, 내보내기 성공 전에는 원본을 지우지 않는다. 보안 상태에 따른
  기존 진단 공개 제한도 유지한다.

### D. 출력 실패가 reset을 막지 않게 구성

- fatal 경로의 첫 동작은 capture와 재진입 guard다. 현재 `lldbg`보다 먼저 수행한다.
- panic 전용 `try_putc`는 TX enable/ready를 검사하고, 문자별 제한과 전체 출력량·시간
  제한을 모두 둔다. 첫 실패 이후에는 UART를 사용 불가로 표시하고 추가 출력을 생략한다.
- 정상 console API의 성공 의미를 조용히 바꾸지 않는다. fatal 모드의 `up_lowputc`와
  flush가 실패 가능한 출력 경로를 사용하도록 연결하고, 지원하지 않는 보드는 fatal
  실시간 출력을 생략한 뒤 다음 부팅에서 기록을 출력한다.
- interrupt가 꺼져도 진행하는 시간원 또는 유한 polling 횟수를 사용한다. SysTick
  interrupt에 의존하는 tick timeout이나 sleep을 사용하지 않는다. MMIO bus 자체의
  정지는 software polling 상한으로 해결되지 않을 수 있으므로 HW watchdog이 받는다.
- 긴 stack/heap dump, host handshake, console 전체 drain은 자동 재부팅을 보장하는
  운영 경로에서 제외한다. 비상 기록 이후 선택적인 진단은 같은 전체 시간 예산 안에서만
  수행한다. fault 상태에서 watchdog disable/kick 반복을 하지 않는다.
- 최소 저장 후 안전한 board/architecture reset hook으로 `SYSRESETREQ`를 요청한다.
  SW reset 실패 시에도 동작하도록 독립 HW watchdog을 **정상 부팅 때부터** arm한다.
  software watchdog/timer callback은 architectural lockup의 대안이 아니다.

진단·출력·reset request·watchdog 만료의 시간 예산은 보드 설정으로 분리한다.
초기 목표안은 software reset request 100 ms 이내, 선택적 UART 출력 20 ms 이내,
HW watchdog 2 s 이내다. 주파수·UART 속도·기존 watchdog 정책을 확인하고 측정하여
확정한다. QEMU host timeout은 이 guest 시간 예산의 증거로 사용하지 않는다.

## 4. 기존 테스트를 개선 검증기로 바꾸는 방법

원래 재현 recipe/관측 자료는 남기고 운영 정책용 recipe와 runner 모드를 추가한다.
같은 `faultlab 1–6` 트리거를 사용하되 `reproduce`와 `recover`의 기대 결과를 분리한다.
[`nested.py`](../../tools/qemu-fault-lab/nested.py)는 현재 여러 fault 정지를 `pass`로
허용하고, [`run.py`](../../tools/qemu-fault-lab/run.py)는 AUTORESET 빌드를 거부한다.
이 판정은 새 `recover` 모드에서 바꿔야 한다.

| 검증 대상 | 통과 기준 | 실패로 판정할 것 |
| --- | --- | --- |
| 1–3 | 동일 IRQ 번호/priority/진입 창, 두 ISR 횟수, mask/SP/limit/callee-saved 보존, fault 없음, 정상 task·TASH/heartbeat 진행 | 임의 spin 진입만 확인, 주입 누락, priority 변경, fault 후 reset을 정상 복귀로 처리 |
| 4 | 최초 fault ID와 유효 record, guest reset 이벤트, 새 boot sequence, 부팅 후 동일 기록 및 정상 heartbeat | timeout, assert 루프, reset 요청만 발생하고 실제 재부팅 없음 |
| 5 | 잘못된 MSP와 2차 fault가 실제 실행됐다는 marker, 비상 진입/record/실제 reset 또는 실기기 HW watchdog reset | 2차 주입 이전 reset으로 건너뜀, QEMU fatal 종료를 reset 성공으로 해석 |
| 6 | TXEN=0/TXFULL 주입 marker, 출력 중단 표시, UART 없이 record commit, reset 후 기록 확인 | UART를 되살려 주입을 무효화, host가 강제 재시작해서 성공으로 기록 |

새 capture가 너무 일찍 reset해서 5·6번의 주입을 건너뛰지 않도록, 테스트 hook은
첫 최소 record 확보 이후·최종 reset 이전의 명시된 위치에서 실행한다. 운영 빌드에는
이 hook을 넣지 않으며, runner는 각 주입 marker를 반드시 검사한다.

재부팅 검증은 같은 QEMU 프로세스의 guest reset을 QMP 등으로 관측하고, reset 전후
boot counter/record를 확인한다. host에서 QEMU를 종료 후 새로 띄우는 작업은 guest
reset이나 RAM retention의 증거가 아니다. timing 검증에는 debugger breakpoint의
정지 시간을 포함하지 않으며, GDB는 별도의 원인 관측에 사용한다.

QEMU 11.1.2의 [NVIC 구현](https://github.com/qemu/qemu/blob/v11.1.2/hw/intc/armv7m_nvic.c#L672)은
일부 lockup에서 `cpu_abort()`로 프로세스를 종료한다. 이미 종료된 QEMU에서 watchdog
만료를 기다리는 시험은 불가능하다. 5번의 lockup 전 방지 경로는 QEMU로 검증하고,
진짜 lockup 이후 watchdog reset은 대상 SoC에서 별도 필수 항목으로 검증한다.

## 5. 구현 순서와 완료 조건

| 단계 | 작업 단위 | 완료 조건 |
| --- | --- | --- |
| 0 | 별도 구현 branch/worktree, reset/retention/HW watchdog 가능성 조사, 기대 결과를 엄격하게 검사하는 recover runner | 원래 baseline에서 1/2/4/5/6을 실패로 잡고 3의 정상 복귀를 판별. 5번 진입 가능성 및 QEMU의 한계를 기록 |
| 1 | 2022년 수정 복원, `up_doirq` flags 결함 수정, 진입·복귀 context 보호 | 1–3의 동일 조건을 각 100회 통과. priority0 문제를 남긴 채 다음 단계에서 정상 처리로 간주하지 않음 |
| 2 | 비상 HardFault 진입, 최소 record·재진입 guard·boot reader | 잘못된 frame을 읽지 않고 첫 fault 보존. 5번의 software 진입 가능 구간에서 실제 reset까지 관측 |
| 3 | bounded panic output/flush, 대화형 crashdump 분리, SW reset + HW watchdog 정책 | 4·6 각 100회 기록 보존 후 reset. UART 미응답·재진입·capture 중 2차 fault에서 상한 준수 |
| 4 | ABI·RTOS·보드 회귀와 실기기 검증 | 지원 빌드·fault 경계 행렬을 통과하고 HW watchdog/retention/디버그 관측 결과를 남김 |

현재 실험 branch는 재현 기준으로 유지하고, 구현 시 그 기준 커밋에서 새 worktree를
만든다. `kernel-tc` 등 다른 worktree의 미커밋 변경을 가져오거나 수정하지 않는다.

회귀 범위:

- QEMU flat와 protected/loadable/XIP의 task switch, SVC, signal, semaphore,
  timer, `kernel_tc` 및 `network_tc`; 기존 4개 recipe의 빌드·partition/size 확인.
- 진입뿐 아니라 복귀 창, nest depth 1/2/3 이상, PSP/MSP, stack limit 경계,
  alignment, 유효/무효 frame, invalid EXC_RETURN, 이미 masked된 상태.
- FPU on/off의 context 레이아웃 및 lazy stacking; 실제 지원 설정만 적용하고
  구성 자체가 지원되지 않는 항목은 명시한다. SMP·TrustZone은 QEMU AN505 단일
  코어 검증과 분리해 해당 보드·security bank에 맞춰 검증한다.
- 전체 fatal path의 stack 사용량·비상 stack canary, record 중간 reset, CRC 불일치,
  연속 crash, log backend 고장, UART clock/gate/MMIO 접근 실패.
- 실기기에서 watchdog 독립 clock/reset 경로, reset 원인, retention 영역, bootloader
  초기화, debug freeze 설정, 재부팅 뒤 T32 attach와 CPUID 접근을 확인한다.
  QEMU GDB 접속 성공으로 T32 `Running (core power down)` 해결을 주장하지 않는다.

## 6. NuttX에서 가져올 것과 별도 설계할 것

[NuttX 참고 조사](QEMU_ARMv8M_Fault_Handling_NuttX_Reference.md)는 upstream commit을
고정해 조사한다. 재진입 시 lock/dump를 반복하지 않고 reset으로 빠지는 구조,
fault context 보존, panic policy의 분리, high-priority IRQ 계약을 참고한다.

현대 NuttX의 예외 진입·PendSV·context ABI는 이 TizenRT와 다르므로 assembly나
register offset을 통째로 가져오지 않는다. NuttX 역시 모든 panic 출력이 유한 시간에
끝나거나 손상된 MSP/architectural lockup을 복구한다고 가정하지 않는다.
비상 진입부, 출력 상한, reset 후 record 보존과 HW watchdog 보장은 대상 TizenRT
보드의 요구사항으로 직접 구현·검증한다.

가장 먼저 실행할 작업은 **recover runner의 실패 기준을 만들고, 패치 포함 상태에서
2번의 priority0 진입과 5번의 HardFault 첫 명령 도달 가능성을 관측하는 것**이다.
이 두 결과가 IRQ 보호 범위와 비상 진입부 설계를 결정한다.
