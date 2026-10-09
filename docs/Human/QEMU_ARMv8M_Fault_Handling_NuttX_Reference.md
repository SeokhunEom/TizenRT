# ARMv8-M fault handling 개선을 위한 NuttX 참고 조사

조사일: 2026-10-09. 이 문서는 **수정 계획을 위한 소스 조사**이며 구현이나
추가 QEMU 실행 결과가 아니다. TizenRT 기준은 `68f42f203`, NuttX는 조사 시점
`master`의 `fdf5ea919e16ea9ea7a70a47de5408f63e61e071`로 고정했다.
소스 링크는 모두 이 커밋을 가리킨다. 선택한 정책은 복구 불가능한 fault에서
**최소 진단 정보를 먼저 보존하고 자동 재부팅**하는 것이다.

## 적용할 결론

1. 중첩 IRQ 진입 불변식과 fault 최종 처리 정책을 별도로 고친다. 정상 IRQ는
   Thread로 복귀해야 하며, 재부팅은 정상 처리의 성공 조건이 아니다.
2. NuttX에서 참고할 것은 재진입 panic의 빠른 reset, 명시적 context 전달,
   보드별 crashdump/reset 분리, 다음 부팅에서 진단을 내보내는 구조다.
3. NuttX의 전체 assert 경로를 복사해도 UART 무한 대기나 architectural lockup은
   해결되지 않는다. 진단의 첫 단계에는 UART, allocator, 일반 device driver,
   스케줄러·파일시스템 의존성을 넣지 않는다.
4. 이미 lockup에 진입한 코어에는 C handler를 실행시켜 복구한다고 약속할 수 없다.
   이 조건은 사전에 동작 중인 하드웨어 watchdog 또는 SoC lockup-reset 연동과
   fault 이전/초기 breadcrumb 보존을 별도로 검증해야 한다.

## NuttX에서 확인한 구현과 TizenRT에 제안하는 적용

| 주제 | 고정된 NuttX 소스에서 확인한 사실 | TizenRT 적용 제안과 제한 |
| --- | --- | --- |
| Panic 재진입 | `_assert()`는 `OSINIT_IS_PANIC()`이면 assert lock을 얻기 전에 `reset_board()`로 간다. | 첫 fault를 덮어쓰지 않는 진입 guard를 출력·lock보다 먼저 둔다. 이미 손상된 MSP에서 C 함수까지 진입하지 못하는 경우는 해결하지 못한다. |
| Fault context | UsageFault/HardFault는 `PANIC_WITH_REGS("panic", context)`로 context를 전달하고 `_assert()`가 별도 배열에 복사한다. | 잘못된 `current_regs`에 의존하지 않도록 최초 raw exception frame과 EXC_RETURN을 명시적으로 전달한다. 읽기 전에 프레임 유효성을 검증한다. |
| 재부팅 정책 | `CONFIG_BOARD_RESET_ON_ASSERT >= 1`이면 `board_reset()`을 호출한다. `0`은 LED/지연 무한 루프이고 `>= 2`는 사용자 thread assert까지 fatal로 취급한다. | 운영 정책은 fatal fault에 reset을 사용하되 사용자 binary 복구 정책과 구분한다. 현재 TizenRT 설정 이름이 같더라도 실제 분기 사용 여부를 확인한다. |
| Crashdump 계약 | `board_crashdump` API 설명은 ISR fault 상태에서 일반 driver를 사용할 수 없고, 안전한 저장소에 machine state를 남긴 뒤 다음 부팅에서 내보내도록 설명한다. | 고정 크기 retention/noinit 레코드를 먼저 기록하고 다음 부팅에서 정상 드라이버로 파일 또는 UART에 내보낸다. 실제 reset의 RAM 유지 여부는 보드별로 입증해야 한다. |
| 첫 진단 보존 | STM32 BBSRAM `savepanic()`은 `once`와 기존 dump의 timestamp로 동일 부팅 재진입/미수거 이전 dump 덮어쓰기를 억제한다. | 최소 레코드에 version, length, sequence, validity, checksum을 두고 완료 표식을 마지막에 기록한다. 첫 원인과 2차 fault 표식을 구분한다. 단순 static bool을 SMP 동기화 기법으로 복사하지 않는다. |
| Reset primitive | ARMv8-M `up_systemreset()`은 AIRCR에 VECTKEY와 SYSRESETREQ를 쓰고 DSB 후 기다린다. MPS2-AN521 `board_reset()`도 이를 호출한다. | 진단 commit 후 저수준 reset을 요청한다. reset 요청이 실제 시스템 재부팅으로 연결되는지와 watchdog fallback을 QEMU/실기기에서 각각 확인한다. |

근거: [panic 진입 및 reset](https://github.com/apache/nuttx/blob/fdf5ea919e16ea9ea7a70a47de5408f63e61e071/sched/misc/assert.c#L808-L933),
[UsageFault](https://github.com/apache/nuttx/blob/fdf5ea919e16ea9ea7a70a47de5408f63e61e071/arch/arm/src/armv8-m/arm_usagefault.c#L62-L118),
[HardFault](https://github.com/apache/nuttx/blob/fdf5ea919e16ea9ea7a70a47de5408f63e61e071/arch/arm/src/armv8-m/arm_hardfault.c#L72-L149),
[reset 설정](https://github.com/apache/nuttx/blob/fdf5ea919e16ea9ea7a70a47de5408f63e61e071/boards/Kconfig#L5848-L5866),
[crashdump 계약](https://github.com/apache/nuttx/blob/fdf5ea919e16ea9ea7a70a47de5408f63e61e071/include/nuttx/board.h#L796-L824),
[BBSRAM 첫 dump 보존](https://github.com/apache/nuttx/blob/fdf5ea919e16ea9ea7a70a47de5408f63e61e071/arch/arm/src/stm32f7/stm32_bbsram.c#L788-L823),
[ARMv8-M reset](https://github.com/apache/nuttx/blob/fdf5ea919e16ea9ea7a70a47de5408f63e61e071/arch/arm/src/armv8-m/arm_systemreset.c#L49-L69),
[MPS2-AN521 reset](https://github.com/apache/nuttx/blob/fdf5ea919e16ea9ea7a70a47de5408f63e61e071/boards/arm/mps/mps2-an521/src/mps2_reset.c#L58-L62).

## 현재 NuttX 예외 진입을 그대로 이식하면 안 되는 이유

확인한 NuttX의 vector table은 NMI부터 PendSV까지 `exception_common`을,
SysTick과 외부 IRQ는 `exception_direct`를 사용한다. `exception_direct()`는
IRQ를 dispatch한 뒤 실행할 task가 바뀌면 PendSV를 pending한다. 따라서
`current_regs`와 `g_nestlevel`을 사용하고 공통 예외 진입에서 스택을 전환하는
이번 TizenRT 구성과 context-switch ABI가 다르다. NuttX 파일의 일부 오래된
주석은 모든 vector가 common entry를 쓴다고 설명하지만, 실제 initializer는
위와 같이 분리되어 있다.
[실제 vector initializer](https://github.com/apache/nuttx/blob/fdf5ea919e16ea9ea7a70a47de5408f63e61e071/arch/arm/src/armv8-m/arm_vectors.c#L95-L111),
[직접 IRQ 진입과 PendSV 요청](https://github.com/apache/nuttx/blob/fdf5ea919e16ea9ea7a70a47de5408f63e61e071/arch/arm/src/armv8-m/arm_doirq.c#L47-L69).

NuttX `exception_common`은 EXC_RETURN으로 PSP/MSP를 선택하며 MSP를 사용하는
경우 software context 공간만큼 MSP를 먼저 내린다. 저장된 BASEPRI,
CONTROL, EXC_RETURN을 복귀 때 되돌린다. Hardware stack-check 설정에서는
MSPLIM/PSPLIM도 context 처리에 포함한다. 이는 **항상 유효한 MSP와 완전한
복귀 context를 유지해야 한다는 참고 근거**다. 검토한 ARMv8-M common/fault/
assert 경로에 별도 emergency fault stack으로 무조건 갈아타는 보편적 해법은
없었으며, TizenRT에 추가한다면 별도의 설계·검증 대상이다.
[진입과 복귀 assembly](https://github.com/apache/nuttx/blob/fdf5ea919e16ea9ea7a70a47de5408f63e61e071/arch/arm/src/armv8-m/arm_exception.S#L116-L266),
[stack-limit 설정](https://github.com/apache/nuttx/blob/fdf5ea919e16ea9ea7a70a47de5408f63e61e071/arch/arm/src/armv8-m/Kconfig#L82-L95).

NuttX의 high-priority IRQ 문서는 BASEPRI 위의 zero-latency IRQ가 OS API를
호출하면 안 되며, OS 작업은 PendSV 등으로 넘기도록 한다. 별도 vector와
유효한 MSP가 필요하다는 제한도 명시한다. Priority `0x00`이 일반 kernel IRQ와
같은 경로를 안전하게 사용할 수 있다는 근거로 이 문서를 해석하면 안 된다.
또한 여기의 우선순위 숫자는 설명용 예시이므로 QEMU와 실제 SoC의 priority bit,
PRIGROUP 및 SVCall/BASEPRI 구성을 기준으로 다시 계산한다.
[고정 버전 high-priority IRQ 가이드](https://github.com/apache/nuttx/blob/fdf5ea919e16ea9ea7a70a47de5408f63e61e071/Documentation/guides/concurrency/zerolatencyinterrupts.rst),
[ARMv8-M priority 정의](https://github.com/apache/nuttx/blob/fdf5ea919e16ea9ea7a70a47de5408f63e61e071/arch/arm/include/armv8-m/nvicpri.h#L37-L85),
[protected/high-priority 조합의 IRQ stack 필수 검사](https://github.com/apache/nuttx/blob/fdf5ea919e16ea9ea7a70a47de5408f63e61e071/arch/arm/src/armv8-m/arm_exception.S#L57-L76).

## 진단 경로 자체가 다시 멈추는 문제

NuttX `_assert()`는 진단 출력 전에 `syslog_flush()`를 호출하고, fatal dump는
일반 진단·task dump·flush 뒤에 `board_crashdump()`를 호출한다. UsageFault와
HardFault 자체에도 panic 호출 전 로그가 있다. 따라서 이 순서를 그대로 쓰면
고장 난 UART가 첫 진단 보존 및 reset을 막을 수 있다.
[assert 출력 순서](https://github.com/apache/nuttx/blob/fdf5ea919e16ea9ea7a70a47de5408f63e61e071/sched/misc/assert.c#L743-L805),
[panic 이후 순서](https://github.com/apache/nuttx/blob/fdf5ea919e16ea9ea7a70a47de5408f63e61e071/sched/misc/assert.c#L904-L931).

또한 Nucleo-F746ZG crashdump 예제는 stack 내용을 복사한 **뒤에** SP 범위가
유효했는지 표시한다. 저장 구조 참고용이지 손상된 stack에서도 안전한 복사
루틴의 근거는 아니다. TizenRT에는 복사 전에 전체 주소 범위, 정렬, 정수
overflow, 읽기 가능한 RAM/보안 영역, stacking error flag를 검사하는 순서를
제안한다. 검사에 실패하면 raw SP와 fault register만 남기고 stack walk를 생략한다.
[BBSRAM 예제의 stack 복사와 검사 순서](https://github.com/apache/nuttx/blob/fdf5ea919e16ea9ea7a70a47de5408f63e61e071/boards/arm/stm32f7/nucleo-f746zg/src/stm32_bbsram.c#L450-L501).

**제안 순서:** 예외 진입 최소 레지스터 확보 → 최초 fault guard → 고정 메모리
진단 commit → 총량과 대기 횟수가 제한된 부가 출력 → 저수준 reset.
Re-entry에서는 lock, stack walk, printf, driver flush를 반복하지 않고 가능한
최소한의 2차 fault 표식만 쓴 뒤 reset으로 간다. Timeout은 인터럽트 기반
tick이나 scheduler만으로 구현하지 않는다. UART 자체 MMIO 접근이 버스에
영구 정체될 수 있는 보드는 CPU의 loop bound만으로 충분하지 않으므로
독립 watchdog이 최종 한계를 보장해야 한다. 이 단락은 NuttX에서 그대로
제공하는 기능이 아니라 **이번 테스트를 위한 TizenRT 개선 제안**이다.

## 여섯 시나리오에 대한 적용 목표

기존 관측의 근거는 [패치 비교 문서](QEMU_ARMv8M_Nested_IRQ_Patch_Comparison.md)다.
아래는 아직 실행하지 않은 개선 후 acceptance 기준이다.

| 번호 | 개선 목표 | 검증 경계 |
| --- | --- | --- |
| 1 `early40` | 2022 PSP/보조 IRQ stack 수정 복원 후 두 IRQ 처리 및 원래 Thread 복귀. BASEPRI·PRIMASK·PSP/MSP·EXC_RETURN·nest depth의 전후 불변식 확인. | 원본 패치 제거 실험 결과를 보존하고 수정 버전은 별도 결과로 기록한다. |
| 2 `early00` | 기존 priority `0x00` 트리거를 유지한 정상 처리·복귀가 목표다. 일반 OS IRQ의 진입 보호와 전용 vector/top-half 및 deferred 처리의 계약을 구분해 설계한다. | 일반 경로에서 priority 0을 거부하거나 전용 경로로 제한하는 대안은 별도 정책 결정이다. priority 변경·주입 누락·fault 후 reset을 기존 2번 정상 처리 성공으로 표시하지 않는다. |
| 3 `late40` | 정상 중첩 처리와 Thread 복귀 유지. | 기존 테스트의 마지막 spin은 의도된 완료 루프다. 제품 HANG과 구분해 완료 플래그·IRQ 횟수·레지스터로 판정한다. |
| 4 UDF | 첫 UsageFault 레코드 보존 → 정해진 시간 안에 guest reboot → 다음 부팅에서 동일 레코드 조회. | 기존 `up_assert` 무한 루프 관측은 개선 전 baseline으로 남긴다. |
| 5 bad MSP + 2차 UDF | handler가 아직 실행 가능할 때는 emergency stack/재진입 guard로 추가 손상을 줄인다. 이미 architectural lockup이면 사전 활성 watchdog 또는 SoC reset 연동으로 재부팅. | 현재 강제 lockup 주입은 C 경로를 우회하거나 handler 진입 전에 죽을 수 있다. QEMU fatal 종료/host 프로세스 재시작을 guest watchdog 복구로 세지 않는다. 전체 stack dump 보존을 보장하지 않는다. |
| 6 UART TXFULL + TXEN=0 | panic 출력은 제한 횟수/총량을 넘으면 생략하고 최소 레코드 보존 후 reset. 일반 console 출력의 오류 계약도 별도로 확인한다. | 원래 6번은 이미 UsageFault 진입 후 테스트 hook에서 UART TX를 끄고 다음 lldbg를 멈추게 한다. 이 순서와 주입 marker를 유지해야 하며, 새 reset 경로가 UART 고장 주입을 건너뛰면 통과가 아니다. |

Arm Cortex-M33 공식 가이드 §2.5.4는 lockup 중에는 명령어가 실행되지 않으며
reset, 가능한 더 높은 우선순위 예외, debugger halt가 탈출 수단이라고 설명한다.
따라서 watchdog은 fault handler가 뒤늦게 시작하는 방식으로만 두지 말고,
일반 동작 중 이미 활성화되어 CPU 중단과 독립적으로 timeout/reset을 발생시킬
수 있어야 한다. Security/clock/reset routing은 실제 SoC의 계약이다.
[Arm Cortex-M33 Devices Generic User Guide, Issue 06, p.67](https://documentation-service.arm.com/static/66be2153882fec713ef49e49#page=67),
[Arm의 watchdog 시스템 구성 예, §13.5](https://developer.arm.com/-/media/Arm%20Developer%20Community/PDF/Common%20Tasks/Creating%20a%20system%20for%20Machine%20Learning%20at%20the%20Edge.pdf?revision=cf93fd3c-6565-4814-a83f-96db08d4354e#page=33).

## TizenRT 구현 착수 전에 확인할 구체 항목

- `up_exception.S`, `up_doirq.c`, context layout, IRQ priority 설정을 함께 본다.
  특히 현재 `CONFIG_ARCH_NESTED_INTERRUPT` 분기의 `irqstate_t flags`는
  `irqrestore(flags)` 전에 C 소스상 초기화가 없다. 실제 생성 assembly 및
  원래 의도한 마스크 복원 계약을 확인하고 별도 결함으로 처리한다.
  이를 priority 0 실패의 입증된 단독 원인이라고 단정하지 않는다.
  [현재 TizenRT up_doirq](../../os/arch/arm/src/armv8-m/up_doirq.c).
- `up_usagefault.c`, `up_hardfault.c` 등에서 context 역참조와 출력보다 먼저
  최소 진단을 저장할 수 있는 entry를 설계한다. 진입 stack은 정적 전용 영역과
  한계를 두되, hardware exception entry 자체가 완료되지 않는 fault를 별도 분류한다.
  [현재 UsageFault](../../os/arch/arm/src/armv8-m/up_usagefault.c),
  [현재 HardFault](../../os/arch/arm/src/armv8-m/up_hardfault.c).
- `up_assert.c`의 기존 user binary 복구, reboot reason, crashdump hook과
  새로운 fatal-reset 상태 전이가 충돌하지 않게 한다. 최초 원인 레코드에
  TCB 포인터를 저장할 수는 있지만 최소 기록 경로에서 검증 없이 TCB를 따라가지 않는다.
  [현재 assert](../../os/arch/arm/src/armv8-m/up_assert.c).
- 현재 공통 `board_crashdump()`는 `CONFIG_WATCHDOG`에서
  `up_watchdog_disable()`을 호출하고 설정에 따라 UART dump를 수행한다.
  자동 reset 정책에서는 이 대화형 dump 경로를 그대로 호출하지 않으며,
  최종 reset용 watchdog을 끄지 않는 별도 최소 저장 경로가 필요하다.
  [현재 crashdump](../../os/board/common/crashdump.c).
- `qemu_armv8m_lowputc()`의 무제한 TXFULL loop를 다룬다. Panic 전용 bounded
  writer와 일반 lowputc의 오류 계약은 따로 정하고 정상 serial 동작 회귀도 확인한다.
  [현재 QEMU UART](../../os/arch/arm/src/qemu-armv8m/qemu_armv8m_serial.c).
- 최소 레코드에 fault 종류, CFSR/HFSR/SHCSR, 유효한 fault address,
  raw EXC_RETURN, PSP/MSP, BASEPRI/PRIMASK/CONTROL, 검증된 frame의 PC/LR/xPSR,
  유효성/재진입/출력생략 표식과 build ID를 둔다. 다음 부팅에서 reset cause와
  대조하고, 수거 전 재부팅 loop에도 첫 레코드를 보존한다.

## 조사 한계

NuttX 소스와 Arm 문서만 읽었다. NuttX를 빌드하거나 TizenRT에 이식하지 않았고,
이번 조사에서 QEMU를 재실행하지 않았다. 기존 QEMU 관측을 실제 T32 attach,
debug authentication, 전원 도메인 또는 대상 보드 watchdog의 증거로 사용하지 않는다.
Emergency stack, bounded panic writer, crash record format은 위 참고에서
도출한 설계 제안이며 NuttX가 여섯 조건을 모두 통과한다는 주장도 아니다.
