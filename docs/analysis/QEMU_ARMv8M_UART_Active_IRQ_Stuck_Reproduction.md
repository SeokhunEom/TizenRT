# QEMU ARMv8-M UART active IRQ stuck 재현 보고서

- 작성일: 2026-10-17
- 기준 소스: `codex/qemu-armv8m-kernel-tc` @ `5c0120f685ff47ab17f7ec26c0554965e312bc27`
- 실행 환경: QEMU 11.1.2, `mps2-an505`, `tizenrt/tizenrt:2.0.1-arm64-local`
- 상태: 재현 성공. 제품 코드 수정은 포함하지 않았다.

## 1. 판정

**재현된다.** UART RX 처리 자체가 끝나고 Cortex-M 예외 복귀 명령(`bx r14`)을 실행하기 직전, UART보다 높은 우선순위의 TIMER0가 들어오면 TIMER0의 스케줄링으로 B가 Thread mode에서 실행될 수 있다. 이때 UART RX IRQ의 NVIC active 비트는 남는다. B가 A를 `task_delete()`하면 UART 예외 복귀를 마칠 A의 저장 문맥도 제거되며, 후속 RX 바이트는 관측 시간 동안 pending으로 남고 UART RX ISR의 처리 횟수가 증가하지 않았다.

경합은 C ISR 본문에서 TIMER0가 중첩되는 일반 상황과 다르다. `up_doirq()`가 소프트웨어 인터럽트 상태를 정리한 다음, 어셈블리 epilogue가 인터럽트 마스크를 복구하고 `bx r14`를 실행하기 전의 짧은 구간이다. CPU/NVIC에서는 UART가 아직 active지만 커널의 `current_regs`는 이미 NULL이고, nested 설정에서는 `g_nestlevel`도 이미 0이다. 이 간극에서 높은 우선순위 TIMER0가 진입하면 커널은 이를 소프트웨어상 최외곽 IRQ로 처리한다.

이 재현은 TizenRT QEMU 보드의 **MPS2 TIMER0 외부 IRQ**(IRQ 19)를 OS tick으로 사용했다. ARM SysTick 예외(IRQ 15)를 직접 시험한 것은 아니다. 같은 우선순위인 기본값 0x80 대 0x80에서는 대조군이 정상 동작했고, 실험에서 TIMER0를 0x40으로 올렸을 때 복귀 직전 경합이 재현됐다. 따라서 “TIMER가 UART보다 높으면 가능한가?”에 대한 QEMU 답은 **그렇다**이며, 재현 조건은 우선순위 관계와 이 복귀 구간의 타이밍이다.

## 2. 재현 설정과 방법

기준 브랜치의 기본 `hello` 설정(`CONFIG_ARCH_INTERRUPTSTACK=0`, nested interrupt 비활성)을 먼저 시험했다. 이어 `CONFIG_ARCH_INTERRUPTSTACK=2048`, `CONFIG_ARCH_NESTED_INTERRUPT=y`인 격리된 `fault_lab_nested` 설정에서도 같은 경합을 시험했다. 두 설정 모두 TIMER0 우선순위 0x40에서 재현됐다. 따라서 이 현상은 `CONFIG_ARCH_NESTED_INTERRUPT` 사용 여부에 한정되지 않는다. 더 높은 우선순위 IRQ를 허용하는 경로에서 소프트웨어 상태와 하드웨어 active 상태가 어긋나는 것이 조건이다.

임시 시험 코드는 TASH 명령 `uartrace`로 A(우선순위 130)와 B(140)를 만들었다. A는 TIMER0를 잠시 멈춘 뒤 watchdog을 1 tick으로 예약하고 실행을 유지한다. QEMU serial로 실제 RX 바이트를 보내면 UART0 RX IRQ 48이 발생한다. UART epilogue에서 시험 코드가 NVIC ISPR0의 TIMER0 비트(IRQ 3, 0x8)를 설정하고, 디버거는 `uartlab_injected` 직후에 멈춰 소프트웨어/하드웨어 상태를 읽었다. 계속 실행하면 더 높은 우선순위 TIMER0가 마스크 복구 후 `bx r14` 전에 진입한다. TIMER0 watchdog callback은 semaphore로 B를 깨우며, B는 A를 삭제한다. 이후 두 번째 RX 바이트를 보내 UART가 다시 처리되는지 확인했다.

디버거는 레지스터·메모리 상태를 읽고 breakpoint/continue만 수행했다. 예외 프레임, `EXC_RETURN`, scheduler 상태 또는 NVIC active 비트를 디버거에서 덮어쓰지 않았다. 재현 시점 제어를 위해 임시 펌웨어 계측이 NVIC ISPR0에 TIMER0를 pending시키고 우선순위를 설정한다. 이 계측은 재현을 위해서만 만든 것이며 제품 코드 변경이나 완화 조치가 아니다. 시험 패치 전체는 [instrumentation.patch](qemu-armv8m-uart-active/evidence/instrumentation.patch)에 보관했다.

## 3. 관측 결과

| 설정 / 주입 시점 | TIMER0 우선순위 | 결과 | 확인한 상태 |
| --- | ---: | --- | --- |
| 기본 `hello`, UART epilogue 직전 | 0x40 (UART 0x80보다 높음) | **UART active 고착** | B가 Thread mode에서 A 삭제. `IABR1 bit0=1`; 두 번째 RX 후 `RXFULL=0x2`, `INTSTATUS=0x2`, UART pending bit가 1이며 RX 처리 횟수는 증가하지 않음. |
| 기본 `hello`, UART epilogue 직전 | 0x80 (UART와 같음) | 정상 | UART active가 먼저 해제되고 두 번째 RX 처리 횟수가 증가함. |
| nested 설정, UART epilogue 직전 | 0x40 | **UART active 고착** | `current_regs=NULL`, `g_nestlevel=0` 상태에서 TIMER0 pending. B가 A를 삭제한 뒤에도 UART active 비트가 1. |
| nested 설정, UART C ISR 본문에서 TIMER0 중첩 | 0x40 | 정상 | nested TIMER0가 UART ISR 본문을 중단한 경우 UART가 복귀를 완료하고 active 비트가 해제됨. |
| nested 설정, UART epilogue 직전 | 0x80 | 정상 | 같은 우선순위 TIMER0는 UART active 중 선점하지 못하고, UART 복귀 후 실행됨. |

기본 `hello`의 실패 결과에서 TIMER0 pending 직전 UART exception은 `IPSR=48`, `current_regs=0`이었다. TIMER0 callback에서도 `IPSR=19`였고 `IABR1=0x1`로 UART RX가 active였다. `MPS2_IRQ_UART0_RX=48`이므로 이 값은 UART RX의 `IABR1 bit0`에 해당한다. B가 실행될 때 `IPSR=0`(Thread mode), A의 TCB에는 `EXC_RETURN=0xfffffff1`, 저장 `xPSR=0x21000030`(UART RX exception 48), 저장 PC `0x10001456`가 남아 있었다. `task_delete(A)`가 성공하고 A TCB가 사라진 뒤에도 `IABR1 bit0`은 계속 1이었다. 두 번째 RX에서 `g_uartlab_rx_count`는 14로 그대로였고 UART RXFULL 및 UART RX INTSTATUS가 각각 `0x2`로 남았다. UART RX의 pending bit도 관측됐다.

실패와 성공 비교는 동일 ELF에서 실행 인자만 바꿔 수행했다. 기본 `hello`의 ELF SHA-256은 `29d1bf572d8082985aedd5a9435b83dd5d1b6c831e3cbb6de4a3908478c1319b`이다. nested 설정의 ELF SHA-256은 `b353ad648e0cb999693cc05c3a10c0b02a13021552dbb3824ab45f26c983cb4e`이다. 실행 결과와 QEMU 예외 로그는 [evidence](qemu-armv8m-uart-active/evidence/) 아래에 있다. 경로 및 빌드 사용자 문자열 정리 방법은 [증거 README](qemu-armv8m-uart-active/evidence/README.md)에 적었다.

## 4. 코드 경로 분석

기준 커밋의 `os/arch/arm/src/armv8-m/up_doirq.c`는 nested 설정에서 `g_nestlevel`을 감소시키고 0이 되면 `current_regs`를 NULL로 만든다(157–162행). nested 비활성 경로도 `current_regs`를 이전 값으로 복구해 최외곽 UART의 C 처리 완료 후 NULL로 돌린다(176–183행). 이 두 동작은 C ISR 처리가 끝난 뒤 수행된다.

`os/arch/arm/src/armv8-m/up_exception.S`는 복귀할 context와 stack을 준비한 뒤 interrupt mask를 복구하고, 다음에 `bx r14`로 하드웨어 exception return을 요청한다(415–431행). 즉, `current_regs`/`g_nestlevel`이 ISR 종료를 나타내는 시점과 NVIC가 UART active 상태를 해제하는 exception return 사이에 실행 가능한 명령 구간이 있다. 시험 코드는 이 구간에서 TIMER0를 pending시켰다.

TIMER0의 `qemu_armv8m_timerisr()`는 tick 만료를 `sched_process_timer()`로 전달한다(`os/arch/arm/src/qemu-armv8m/qemu_armv8m_timerisr.c:66-72`). 준비된 B가 더 높은 task priority를 가지면 scheduler가 전환한다. `up_reprioritize_rtr()`는 `current_regs`가 있으면 그 예외 문맥을 기존 TCB에 저장하고 다음 TCB의 문맥을 복원한다(`os/arch/arm/src/armv8-m/up_reprioritizertr.c:172-192`). 이 경합에서는 TIMER0가 UART의 실제 active 상태를 보존한 채 Thread B로 돌아간다. A의 저장 context는 `EXC_RETURN=0xfffffff1`과 UART exception 번호를 가진 handler 복귀 문맥이었다. B가 이를 삭제하면 원래 UART handler의 exception return 경로가 사라진다.

QEMU IRQ 초기화는 외부 IRQ 우선순위를 기본 0x80으로 설정한다(`os/arch/arm/src/qemu-armv8m/qemu_armv8m_irq.c:43-47,129-130`). TIMER0도 초기화 때 0x80을 설정한다(`qemu_armv8m_timerisr.c:79-82`). 그래서 기본 우선순위만 둔 대조군은 이 고장을 내지 않았다. 이 실험은 A가 TIMER0를 0x40으로 올려 TIMER0가 UART보다 먼저 처리되게 했다. IRQ 번호 정의는 `os/arch/arm/include/qemu-armv8m/irq.h:45-48`에 있다.

## 5. 영향과 범위

재현 결과는 UART RX 서비스가 멎는 QEMU kernel availability 문제다. UART가 입력 데이터를 더 받아도 기존 IRQ가 active로 남으므로 같은 UART IRQ handler가 재진입하지 않고, RXFULL 및 pending 상태가 누적될 수 있다. UART를 통해 전달되는 shell/control 입력도 멈출 수 있다.

현재 QEMU `hello`의 TIMER0 및 UART 기본 우선순위는 모두 0x80이어서 이 특정 우선순위 관계는 기본 설정에서 발생하지 않았다. 제품 설정에서 TIMER0/OS tick을 UART보다 높게 지정하거나 다른 higher-priority scheduler IRQ를 쓰면 조건이 성립할 수 있다. QEMU 11.1.2의 MPS2-AN505 모델에서의 재현이며 RTL8721CSM/T32 실기에서의 동작과 빈도는 이 보고서가 증명하지 않는다. 주입은 정확한 경합 지점을 강제로 재현하므로 자연 발생 확률도 측정하지 않았다.

완화 방향을 검토할 때는 context switch를 실제 최외곽 하드웨어 exception return 뒤로 미루거나, interrupt 복귀 epilogue 동안 하드웨어 active 상태와 scheduler의 `current_regs`/nest 상태가 불일치하지 않도록 해야 한다. TIMER0를 UART와 같거나 낮은 우선순위로 제한하는 방법은 해당 설정에서 경합을 막지만, 일반적인 exception-return 안전성을 대신하지 않는다. 이번 작업에서는 수정안을 적용하지 않았다.

## 6. 재현 방법과 보관 파일

임시 worktree의 기준은 위 커밋이다. 그 커밋을 새 worktree에 체크아웃하고 `instrumentation.patch`를 적용한 다음, 기본 및 nested 설정을 각각 빌드한다. 예시는 기본 설정이다.

```sh
git worktree add /tmp/tizenrt-uart-repro 5c0120f685ff47ab17f7ec26c0554965e312bc27
cd /tmp/tizenrt-uart-repro
git apply --unidiff-zero /path/to/docs/analysis/qemu-armv8m-uart-active/evidence/instrumentation.patch
cd os
/opt/homebrew/bin/bash ./dbuild.sh qemu-armv8m hello build
cd ..
python3 tools/uart-active-repro/reproduce.py \
  --elf build/output/bin/tinyara \
  --out tools/uart-active-repro/new-run \
  --mode 0 --priority 0x40
```

`--priority 0x80`은 같은 우선순위 대조군이다. nested config는 같은 `dbuild.sh` 명령에서 `hello`를 `fault_lab_nested`로 바꿔 실행한다. `--mode 1 --priority 0x40`은 nested 설정에서 TIMER0를 UART C handler 본문 중 pending시키는 대조군이다. 각 출력 디렉터리에는 `result.json`, serial log, RSP trace, QEMU exception log가 생성된다.

- [기본 설정 high-priority 재현 결과](qemu-armv8m-uart-active/evidence/hello-p40/result.json)
- [기본 설정 equal-priority 대조 결과](qemu-armv8m-uart-active/evidence/hello-p80/result.json)
- [nested 설정 high-priority 재현 결과](qemu-armv8m-uart-active/evidence/nested-p40/result.json)
- [nested 설정 equal-priority 대조 결과](qemu-armv8m-uart-active/evidence/nested-p80/result.json)
- [nested ISR 본문 중첩 대조 결과](qemu-armv8m-uart-active/evidence/nested-body-p40/result.json)
- [실험 계측 소스 및 재현 runner patch](qemu-armv8m-uart-active/evidence/instrumentation.patch)
