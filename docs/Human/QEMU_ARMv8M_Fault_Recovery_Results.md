# ARMv8-M 중첩 IRQ와 fatal fault 복구

2026-10-09, `codex/qemu-armv8m-fault-tests`.
운영 정책은 **최소 진단 보존 후 자동 재부팅**이다. 정상 IRQ는 원래 실행으로 복귀한다.

## 커밋 구성

1. `8393684ab`: 2022년 `4ee6ceb9ee4dae678d1fc54348a5dc49070b6747`의
   PSP 저장·복원과 보조 IRQ 스택 수정을 복원했다. 기존 QEMU 주입 hook을 유지한다.
2. 이 문서를 포함한 수정 커밋: priority 0 진입/복귀 보호, 최소 fatal 진입부,
   진단 보존·재부팅, 테스트와 아래 조사·계획·실행 근거를 포함한다.

사용자의 최종 지시에 따라 기존 브랜치를 사용했고, 두 커밋을 새로 추가했다.
다른 worktree의 미커밋 수정은 포함하지 않았다.

## 6개 시나리오 결과

QEMU 11.1.2 `mps2-an505`, Cortex-M33, flat / FPU off / 단일 CPU,
`fault_lab_nested` 기준이다. Docker `tizenrt/tizenrt:2.0.1-arm64-local`의 GCC 10.3을 사용했다.
이 표의 통과는 과거의 “정지 재현 성공”과 다른 기준이다.

| 번호 / TASH 명령 | 2022 패치만 포함한 기준선 | 이번 수정 후 | 반복 검증 |
| --- | --- | --- | --- |
| `faultlab 1` — 진입 중 priority `0x40` | 두 IRQ 처리 후 테스트 spin | 두 IRQ가 각각 1회 처리, 원래 TASH 복귀, 별도 task 실행 | 100/100 |
| `faultlab 2` — 진입 중 priority `0x00` | INVSTATE panic | priority를 유지한 채 처리·TASH 복귀·별도 task 실행 | 100/100 |
| `faultlab 3` — ISR 내부 priority `0x40` | 두 IRQ 처리 후 테스트 spin | 기존 중첩 처리 유지, TASH 복귀·별도 task 실행 | 100/100 |
| `faultlab 4` — UDF | UsageFault 진단 뒤 panic 무한 루프 | UsageFault PC/CFSR 보존 → guest reset → 다음 부팅에서 확인 | 100/100 |
| `faultlab 5` — fault 처리 중 잘못된 MSP + UDF | HardFault 공통 진입 중 재차 fault → QEMU lockup 종료 | 별도 스택에서 HardFault 처리, 최초 진단 + 보조 기록 보존 후 reset | 100/100 |
| `faultlab 6` — fault 처리 중 UART TX 정지 | TXFULL polling 무한 루프 | 총 polling 예산 소진 기록, 출력 포기 후 reset | 100/100 |

4–6번은 호스트가 QEMU를 다시 실행하거나 QMP reset 명령을 보내지 않는다.
같은 프로세스에서 `RESET {guest:true, reason:guest-reset}` 이벤트, 유효한 진단,
새 TASH 프롬프트와 추가 명령의 응답을 모두 요구한다.
최초 PC는 ELF의 `qemu_fault_lab_trigger`와 일치해야 하며 CFSR의 UNDEFINSTR도 검사한다.

5번의 필수 추가 관측은 `secondary=3`, `injection=5`,
`secondary_msp=5fffffe0`, `secondary_cfsr=00011000`, `secondary_frame_valid=0`이다.
즉 잘못된 주소를 읽어 PC를 꾸미지 않고, stacking 실패를 기록한다.
새 비상 스택의 MSPLIM이 테스트를 먼저 차단하지 않도록 **5번 주입 코드에서만**
MSPLIM을 0으로 만든 다음 MSP를 `0x60000000`으로 바꾼다.
이 변경 없이 통과한 초기 실험은 잘못된 MSP 복구의 최종 증거로 사용하지 않았다.

6번은 최초 기록을 commit한 뒤 실제 UART의 TXEN을 끄고 버퍼를 채운다.
`injection=6`과 `uart_timeout=1`이 있어야 통과하므로 주입 전에 reset하는 구현은 실패한다.

명령 전송부터 복구 프롬프트까지 호스트에서 잰 시간은 다음과 같다.
실제 코어의 WCET, reset 요청 자체의 지연 또는 실제 보드 시간 보장은 아니다.

| 번호 | 중앙값 | 최대 |
| --- | ---: | ---: |
| 1 | 4.50 ms | 10.92 ms |
| 2 | 4.66 ms | 13.37 ms |
| 3 | 5.17 ms | 20.63 ms |
| 4 | 48.73 ms | 97.06 ms |
| 5 | 49.25 ms | 102.49 ms |
| 6 | 48.78 ms | 98.11 ms |

## 원인과 수정 근거

### 정상 중첩 IRQ

BASEPRI는 priority 0 IRQ를 차단하지 못한다. 기존 진입부는 MSP를 PSP로 옮겨
software frame을 만드는 동안 중첩 IRQ를 허용했다. 최종 복귀 중에도 같은 종류의
일관성 문제가 생길 수 있다.

`up_exception.S`는 원래 PRIMASK를 저장하고, context 생성 및 반환 구간을
PRIMASK로 보호한다. IRQ 스택과 `current_regs`/`g_nestlevel`이 준비되면
기존 BASEPRI와 PRIMASK를 복원해 ISR 동안의 중첩을 허용한다.
PRIMASK는 각 진입의 별도 C 호출 프레임에 보관하므로 task context의
`REG_*` 인덱스나 크기를 다시 바꾸지 않는다. C 호출 스택은 8-byte 정렬한다.

`up_doirq()`의 초기화되지 않은 `flags` 복원을 제거했다. 반환 상태를 갱신할 때
PRIMASK를 설정하고 assembly가 최종 복원한다. 별도 lazy-FPU assembly 경로는
기존 BASEPRI 계약을 유지한다. Flat PSP 복귀 시에도 보조 MSP와 MSPLIM을 준비한다.

원인 확인용 대조 실험에서는 **진입부 `cpsid i` 하나만 제거**했다.
새 테스트·진단·반환 코드는 그대로 둔 상태에서 1·3번은 통과하고 2번만
HardFault 후 reset하여 정상 IRQ 복귀 기준에 실패했다. 이후 원본 수정으로 복원했다.
이 실험으로 초기화되지 않은 `flags`만 고치는 것으로 충분하다고 판단하지 않았다.

priority 0 ISR에서 임의의 OS API까지 안전하다는 의미는 아니다. 해당 ISR은
BASEPRI 기반 kernel critical section을 선점할 수 있다. 이번 priority 0 ISR은
카운터만 갱신하며, 운영 보드의 priority/OS 호출 계약은 별도로 지켜야 한다.

### Fatal fault

`CONFIG_ARMV8M_FAULT_RECOVERY`를 선택하면 vector 3–6이
`up_fault_entry.S`로 직접 들어간다. 기존 스택에 `push`나 C 호출을 하기 전에
MSP/PSP/EXC_RETURN 및 마스크를 확보하고 1536-byte 비상 스택으로 전환한다.
재진입은 별도의 1536-byte 스택을 사용한다. 기존 HardFault의 SVC 대행과 충돌하지
않도록 USEBASEPRI 구성에만 허용하며, 현재 기능 지원은 QEMU flat / FPU off / 단일 CPU로 제한한다.

최초·보조 기록은 각자 version, sequence, checksum, 완료 magic을 가진다.
보조 기록 작성 중 문제가 생겨도 최초 기록의 유효성은 바꾸지 않는다.
기록 영역은 linker의 `.fault_record (NOLOAD)`로 data 뒤, `_sbss` 앞에 두어
startup BSS 초기화와 heap에서 제외한다. QEMU guest reset 뒤 실제 보존을 확인했다.
전원 차단, 이미지 재다운로드, bootloader의 RAM 초기화까지 보존하는 저장소는 아니다.

기본 hardware frame만 지원한다. EXC_RETURN 형식, 정렬, QEMU의 알려진 RAM 범위,
CFSR stacking/unstacking/lazy-state/stack-overflow 오류를 확인한 뒤에만 프레임을 읽는다.
유효하지 않은 프레임은 `frame_valid=0`이며 임의 주소를 probe하지 않는다.

기록 경로는 heap, 파일, scheduler lock, TCB 순회, stack dump, 대화형 crashdump,
console flush를 사용하지 않는다. 정상 boot에서 보안 정책을 확인하고 진단을 출력한다.
Fatal UART 출력에만 전체 4096회의 시도 예산을 적용하고, 이후 출력은 버린다.
SYSRESETREQ는 기존 `up_systemreset()`을 사용한다.
일반 assert 및 앱 복구 정책 전체를 바꾸는 수정은 아니다.

## 실행 방법

저장소의 `os/`에서 Homebrew Bash로 빌드 메뉴를 실행한다.

```sh
/opt/homebrew/bin/bash ./dbuild.sh menu
```

`5. Clean Build and Re-Configure` → `qemu-armv8m` → `fault_lab_nested` →
`1. Build with Current Configuration` 순서로 선택한다. TASH에서는
`faultlab`이 메뉴를 출력하고 `faultlab 1`부터 `faultlab 6`까지 선택한다.
1–3번은 같은 세션에서 반복할 수 있다. 4–6번은 진단을 남기고 guest를 재부팅한다.
`fault_lab`은 IRQ 스택/중첩 기능이 없는 별도 구성으로 4–6번만 지원한다.
해당 구성에서 중첩 테스트의 스택 심벌을 링크하던 문제도 조건부 빌드로 수정했고,
1–3번을 선택하면 `fault_lab_nested` 구성이 필요하다는 메시지를 출력한다.
비중첩 구성의 4–6번도 각각 3회, 총 9회 기록·guest reset·TASH 응답을 확인했다.
[비중첩 복구 결과](../evidence/qemu-fault-lab/20261009/recovery/non-nested-explicit/results.json).

저장소 루트에서 자동 검증한다. `--out`은 아직 없는 경로여야 한다.

```sh
python3 tools/qemu-fault-lab/recover.py \
  --out build/qemu-fault-lab/my-recovery --repeat 100
```

한 가지만 실행하려면 `--cases 5`, 일부만 실행하려면 `--cases 1,2,3`을 추가한다.
이 도구는 대상 register/memory 쓰기나 breakpoint를 사용하지 않는다.
`nested.py`는 별도의 debugger 관찰 도구이고, 과거 `run.py`는 정지 재현 도구이므로
복구 성공 판정에 사용하지 않는다.

## 검증 근거

- [600회 결과와 시간 집계](../evidence/qemu-fault-lab/20261009/recovery/summary.json)
- [전체 실행 결과](../evidence/qemu-fault-lab/20261009/recovery/strict-100/results.json),
  [600회 serial 기록](../evidence/qemu-fault-lab/20261009/recovery/strict-100/serial.log),
  [ELF/config/source 식별 정보](../evidence/qemu-fault-lab/20261009/recovery/strict-100/metadata.json)
- [개선 전 기준선 실패 8회](../evidence/qemu-fault-lab/20261009/recovery/red-baseline/results.json)
- [진입 마스크 제거 대조 실험](../evidence/qemu-fault-lab/20261009/recovery/entry-mask-negative-control/mutation.json)
- [IRQ 진입·복귀 register 관측](../evidence/qemu-fault-lab/20261009/recovery/entry-observation/results.json)
- [스택 접근 전 전환하는 실제 명령어](../evidence/qemu-fault-lab/20261009/recovery/fatal-entry-disassembly.txt),
  [NOLOAD/BSS/stack/heap 배치](../evidence/qemu-fault-lab/20261009/recovery/final-nested-image/memory-layout.txt)
- [근거 파일 SHA-256 목록](../evidence/qemu-fault-lab/20261009/recovery/SHA256.json)

기준선 검증에는 앞선 비교에서 보존한 patch-present ELF를 사용했다. 현재 소스와
섞어 빌드하지 않았으며 ELF SHA-256으로 구분한다. 개선 빌드는 두 번째 커밋을
만들기 전 작업 트리에서 실행했으므로 metadata의 `head`는 복원 커밋 `8393684ab`이다.
실제로 빌드한 개선 소스는 `working_source_sha256`으로 식별하며 최종 소스와 일치함을
검사했다. 2·4·5·6번을 각각 2회 검사해
정지/lockup을 실패로 잡았다. IRQ register 관측은 별도의 debugger 실행이고,
600회 timing/복구 검증에는 debugger를 연결하지 않았다.

일반 회귀는 `.github/scripts/qemu-armv8m-kernel-tc.py`의 기존 network/kernel
프로토콜을 사용한다. `fault_lab_nested`는 보존한 최종 ELF를 지정해 같은 프로토콜로
검사했다. 나머지 recipe는 각각 메뉴의 clean/reconfigure 후 빌드·partition/size를
확인한 실제 바이너리다.

| 빌드 구성 | Network TC | Kernel TC | 실패 |
| --- | ---: | ---: | ---: |
| `fault_lab_nested` (최종 개선 ELF) | 161 | 459 | 0 |
| `hello` (기능 opt-out, flat) | 161 | 459 | 0 |
| `loadable_all` (protected) | 161 | 447 | 0 |
| `xip_all` (protected/XIP) | 161 | 447 | 0 |
| `loadable_apps` (protected/app loading) | 161 | 447 | 0 |

세부 집계와 실행 로그는 [regression 근거](../evidence/qemu-fault-lab/20261009/recovery/regression)에 있다.
위 테스트에는 task switch, signal, semaphore, timer와 SVC 경로가 포함된다.
네트워크의 외부 public ping은 환경상 응답이 없었으나 필수 DHCP·gateway IPv4/IPv6 ping·
DNS·Network TC 기준은 통과했다. protected 빌드의 기존 generated proxy/stub 경고는
이번 변경의 compiler 오류와 구분한다.

## 조사, 계획 및 실제 적용의 경계

- [NuttX 조사와 고정된 upstream 근거](QEMU_ARMv8M_Fault_Handling_NuttX_Reference.md)
- [구현 전 계획 및 검토 항목](QEMU_ARMv8M_Fault_Handling_Plan.md)
- [과거 패치 제거/포함 비교](QEMU_ARMv8M_Nested_IRQ_Patch_Comparison.md)

NuttX의 재진입 처리·진단 전달 원칙을 참고했다. 최신 NuttX의 PendSV/context ABI나
보드 BBSRAM 코드를 그대로 이식하지 않았다. 이번 기록의 firmware 대응은 함께 보존한
ELF/config/source SHA-256 근거로 한다. 운영 보드의 기록에는 별도 firmware ID,
보안·MPU·cache·retention 계약과 flash/백업 RAM 수거 정책을 추가해야 한다.

**이미 architectural lockup에 들어간 코어는 이 C/assembly 경로로 복구할 수 없다.**
이번 5번은 HardFault 진입 명령을 실행할 수 있는 시점에서 lockup을 예방한 결과다.
첫 명령 이전 lockup, MMIO bus 자체 정지, reset 요청 실패에는 fault 전에 켜 둔
독립 하드웨어 watchdog이 필요하다. 이번 커밋은 실제 SoC watchdog을 구현·검증하지 않는다.
T32의 `running(core power down)`과 attach 실패, 실제 전원·debug 도메인도 QEMU 결과로
해결됐다고 판단하지 않는다. FPU/SMP/TrustZone 전환과 실제 보드 검증은 별도 항목이다.

초기 계획에서 더 넓게 제시한 depth 3 이상, 복귀 창의 별도 IRQ 주입,
모든 EXC_RETURN/stack-limit 경계, capture 중간 reset 및 의도적인 checksum 손상,
비상 스택의 최대 사용량 측정은 이번 6개 재현의 완료 조건과 구분한다.
이 조합을 전부 검증했다고 주장하지 않으며 실제 보드 포팅 전 추가 검증 항목으로 남긴다.
