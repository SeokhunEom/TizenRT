# QEMU ARMv8-M 중첩 IRQ 수정 제거 비교

2026-10-09에 QEMU 11.1.2 `mps2-an505`에서 2022년 중첩 IRQ 수정 전후를
같은 NVIC 스케줄로 비교했다. 패치가 없는 빌드에서는 첫 예외 진입 중
중첩 IRQ를 받은 경우 UsageFault가 HardFault로 에스컬레이션된 뒤
`up_assert()` 무한 루프에 도달했다. 패치가 있는 빌드는 priority `0x40`
조건에서 정상 복귀했다. 가장 높은 priority `0x00` 조건은 양쪽에서
정지했으며, ISR 스택 전환 후 중첩시킨 조건은 양쪽에서 정상 복귀했다.

따라서 **특정 중첩 IRQ 타이밍이 패치 제거 빌드에서 panic 정지를 만든다**는
것은 재현했다. 다만 기존에 확인한 세 가지 정지 형태 전체가 중첩 IRQ만으로
자연 발생한다는 뜻은 아니다. 잘못된 MSP로 인한 CPU lockup과 UART polling
대기는 아래 별도 명시적 fault 주입으로 재현했으며, 둘 다 패치 포함 빌드에서도
같이 재현됐다.

## 실험 구성

현재 실험 브랜치는 `codex/qemu-armv8m-fault-tests` 하나다. 기준선은
`5c0120f685ff47ab17f7ec26c0554965e312bc27`이며 그 위에 두 커밋을 둔다.

1. `765565899`: 2022년 원본 수정 `4ee6ceb9e`를 제거한다.
2. 테스트 코드, 1–6번 선택자, 패치 포함 상태를 만드는 patch 파일, 결과 JSON과
   로그, 설명 문서를 추가한다.

기본 checkout은 패치 제거 상태다. `tools/qemu-fault-lab/patch-present.patch`를
적용하면 기존 패치 포함 비교 브랜치와 같은 코드 및 설정이 된다. 이 파일에는
PSP 저장·복원, 보조 IRQ 스택, 관련 설정과 BASEPRI 설정 직후의 테스트 hook
위치가 포함된다. 같은 브랜치에서 상태만 바꾸므로 별도 비교 브랜치는 필요 없다.

최초 검증은 패치 제거 `86d69b242`, 패치 포함 `3c1a1ea7f` 이력에서 수행했다.
원래 관측의 커밋·ELF·설정 해시는 [증거 manifest](../evidence/qemu-fault-lab/20261009/manifest.json)에
보존했다. 이 값은 과거 실행의 출처이며 현재 통합 커밋의 해시를 뜻하지 않는다.

두 빌드 모두 `CONFIG_ARCH_NESTED_INTERRUPT=y`,
`CONFIG_ARCH_INTERRUPTSTACK=2048`,
`CONFIG_REG_STACK_OVERFLOW_PROTECTION=y`로 빌드했다. 패치 포함 설정에는
추가로 `CONFIG_ARCH_NESTED_IRQ_STACK_SIZE=512`가 있다. 테스트용 Thread/PSP와
인터럽트 MSP는 서로 다른 유효 영역을 사용한다. 자연 스케줄 행렬은 fault
register를 쓰지 않고, GDB로 대상 메모리나 레지스터를 변경하지 않는다.
게스트의 QEMU 전용 훅이 NVIC pending 비트만 설정한다.

세 조건을 각각 세 번 실행했다. `early40`와 `early00`은 외부 IRQ 86의 첫
`exception_common` 진입 창에서 IRQ 87을 각각 priority `0x40`, `0x00`으로
중첩시킨다. `late40`은 바깥 ISR이 인터럽트 스택으로 전환한 뒤 priority
`0x40` 중첩 IRQ를 건다. 패치 포함 빌드의 훅은 패치 코드가 BASEPRI를 설정한
직후에 실행되어, BASEPRI로 막히지 않는 높은 priority 중첩만 시험한다.

## 실제 중첩 IRQ 결과

| 조건 | 패치 제거, 3회 | 패치 포함, 3회 |
| --- | --- | --- |
| `early40` — 첫 진입 창, nested priority `0x40` | 3/3 UsageFault/HardFault 후 `up_assert()` 정지 | 3/3 두 IRQ 처리 후 Thread 복귀 |
| `early00` — 첫 진입 창, nested priority `0x00` | 3/3 UsageFault/HardFault 후 `up_assert()` 정지 | 3/3 INVSTATE UsageFault/HardFault 후 `up_assert()` 정지 |
| `late40` — ISR 스택 전환 후 nested priority `0x40` | 3/3 두 IRQ 처리 후 Thread 복귀 | 3/3 두 IRQ 처리 후 Thread 복귀 |

패치 제거 빌드의 `early40`/`early00`에서는 바깥 ISR 처리 횟수가 0이고
중첩 ISR 처리 횟수만 1이었다. QEMU 예외 로그는
`UFSR.UNALIGNED`를 기록했고, 관측한 CFSR은 `0x01000000`, HFSR은
`0x40000000` (`FORCED`)였다. 이후 `up_hardfault()`에서 panic 경로로 들어가
`up_assert+0x490`에 도달했다. 이 주소의 명령은 `e7fe` (`b .`)이며, 0.2초 뒤
추가 표본에서도 같은 PC를 확인했다. 즉, 디버거 breakpoint에서 멈춘 것만이
아니라 fault 처리 이후 실제 영구 정지 루프까지 확인했다.

패치 포함 `early40`은 두 IRQ 횟수가 각각 1이고 Thread 루프까지 복귀했다.
반면 패치 포함 `early00`은 CFSR `0x00020000` (`INVSTATE`)과 HFSR
`FORCED`를 기록하고 `up_assert+0x4da`에 정지했다. 이 테스트는 priority 0
인터럽트까지 패치가 해결한다고 입증하지 않는다.

| 행렬 | 반복 결과 JSON |
| --- | --- |
| 패치 제거 | [results.json](../evidence/qemu-fault-lab/20261009/patch-removed/matrix/results.json) |
| 패치 포함 | [results.json](../evidence/qemu-fault-lab/20261009/patch-present/matrix/results.json) |

각 실행 폴더의 `result.json`, `serial.log`, `exceptions.log`, `stderr.log`,
`rsp.log`와 effective config 및 소스 스냅샷을 Git에 포함했다. ELF는 로컬
복구 기록에 별도 보관하고 그 SHA-256은 결과 JSON에 기록했다.

## 세 fault 정지 형태 재검증

기존 fault-lab의 TASH 명령은 실제 nested IRQ 버그를 유발하는 실험이 아니라,
각 정지 형태를 명시적으로 주입하는 별도 검증이다.

| 주입 | 패치 제거 | 패치 포함 | 주입 내용 |
| --- | --- | --- | --- |
| panic halt | 통과 | 통과 | `UDF` 후 `up_usagefault()` → `up_assert()` |
| QEMU lockup | 통과 | 통과 | UsageFault 처리 중 MSP를 `0x60000000`으로 바꾸고 추가 `UDF` |
| UART polling 대기 | 통과 | 통과 | UART TX를 끄고 TX 버퍼를 채운 뒤 polling 출력 |

양쪽 빌드 모두 각 명시적 주입 조건을 통과했다. 따라서 이 세 행은 패치
제거가 원인이라는 증거가 아니다. 자연 IRQ 행렬에서는 lockup이나 UART
polling 대기가 나오지 않았다. 자연 중첩 조건에서 추가로 확인된 정지 형태는
`up_assert()` panic halt다.

| 명시적 주입 결과 | 반복 결과 JSON |
| --- | --- |
| 패치 제거 | [results.json](../evidence/qemu-fault-lab/20261009/patch-removed/explicit/results.json) |
| 패치 포함 | [results.json](../evidence/qemu-fault-lab/20261009/patch-present/explicit/results.json) |

## TASH에서 1–6번으로 선택 실행

테스트용 `faultlab` TASH 명령은 두 실험 빌드 모두에서 숫자 선택자를 지원한다.
세 중첩 IRQ 조건과 세 명시적 정지 주입을 한 `fault_lab_nested` 이미지에
포함했으므로, 패치 제거/포함 각각 같은 방식으로 선택할 수 있다.

| 명령 | 조건 |
| --- | --- |
| `faultlab 1` | `early40`: 첫 예외 진입 창, 중첩 IRQ priority `0x40` |
| `faultlab 2` | `early00`: 첫 예외 진입 창, 중첩 IRQ priority `0x00` |
| `faultlab 3` | `late40`: 인터럽트 스택 전환 뒤 중첩 IRQ |
| `faultlab 4` | UsageFault → panic halt 명시 주입 |
| `faultlab 5` | 잘못된 MSP와 2차 fault로 architectural lockup 명시 주입 |
| `faultlab 6` | UART TX 중단으로 polling 대기 명시 주입 |

인자 없이 `faultlab`을 실행하면 선택 목록을 출력한다. 각 선택은 의도적으로
TASH 프롬프트로 돌아오지 않는다. 따라서 한 번 부팅한 게스트에서는 한 조건만
실행하고, 다음 조건은 QEMU를 재부팅해 실행한다. 자동 검증기
`nested.py`와 `run.py`도 이 숫자 명령을 각각 보내며, 각 조건을 새 QEMU
인스턴스에서 검증한다.

숫자 선택자 경로도 패치 제거/포함 빌드에서 각 1회 실행했다. 중첩 세 조건은
각 패치 상태에서 기존 행렬과 같은 결과를 냈고, 명시 주입 4–6은 양쪽 모두
각각 의도한 정지 관측을 통과했다.

| 빌드 | 숫자 1–3 중첩 IRQ | 숫자 4–6 명시 주입 |
| --- | --- | --- |
| 패치 제거 | [results.json](../evidence/qemu-fault-lab/20261009/patch-removed/numbered-nested/results.json) | [results.json](../evidence/qemu-fault-lab/20261009/patch-removed/numbered-explicit/results.json) |
| 패치 포함 | [results.json](../evidence/qemu-fault-lab/20261009/patch-present/numbered-nested/results.json) | [results.json](../evidence/qemu-fault-lab/20261009/patch-present/numbered-explicit/results.json) |

## HANG 유형과 정상 처리 뒤 테스트 루프

TASH 프롬프트로 돌아오지 않는 현상만으로 fault 발생을 판정할 수는 없다.
중첩 IRQ 테스트 1–3은 별도 PSP에서 실행한 뒤
`qemu_fault_lab_nested_spin` 무한 루프에 머물도록 설계했다. 따라서 정상
복귀한 조건도 프롬프트는 돌아오지 않는다. 자동 검증기는 Thread 복귀와
두 ISR 처리 횟수를 읽어 정상 처리 여부를 확인한다.

| 번호 | 패치 제거 | 패치 포함 | 멈추는 위치 또는 관측 |
| --- | --- | --- | --- |
| 1 `early40` | fault 후 panic 정지 | 정상 IRQ 처리 뒤 테스트 루프 | 제거: `up_assert`; 포함: `qemu_fault_lab_nested_spin` |
| 2 `early00` | UNALIGNED fault 후 panic 정지 | INVSTATE fault 후 panic 정지 | 양쪽 모두 `up_assert` |
| 3 `late40` | 정상 IRQ 처리 뒤 테스트 루프 | 정상 IRQ 처리 뒤 테스트 루프 | 양쪽 모두 `qemu_fault_lab_nested_spin`; IRQ 버그에 의한 HANG은 관측되지 않음 |
| 4 panic 주입 | panic 정지 | panic 정지 | UsageFault 처리 후 `up_assert` 무한 루프 |
| 5 lockup 주입 | architectural lockup | architectural lockup | 잘못된 MSP로 HardFault 진입 중 다시 fault; QEMU fatal 보고 후 프로세스 종료 |
| 6 UART 주입 | UART polling 무한 대기 | UART polling 무한 대기 | `qemu_armv8m_lowputc`에서 `TXFULL=1`, `TXEN=0` |

1–2는 중첩 IRQ 스케줄로 발생한 fault이며, 4–6은 테스트 코드가 정지 조건을
명시적으로 주입한다. 5는 QEMU 프로세스가 계속 실행되며 기다리는 형태가
아니라 fatal lockup으로 종료되는 결과다. 모든 선택은 새 게스트에서 각각 실행한다.

## T32 Running (core power down) 및 attach 실패와의 관계

Lauterbach는 Cortex-M의 CPUID 읽기가 오류를 반환하거나 유효하지 않은 값을
반환할 때 T32가 `Running (core power down)` 상태를 표시한다고 설명한다.
그러므로 panic 루프나 UART polling을 관측했다는 사실만으로 이 T32 표시나
실제 전원 차단이 발생했다고 판단할 수 없다.
[Lauterbach Support 설명](https://support.lauterbach.com/community/view/why-doesn-t-t32-stop-when-loading-elf)

여섯 테스트 코드에는 코어 전원이나 클럭을 차단하는 요청이 없다. 5번의
architectural lockup은 fault 처리 실패를 재현하지만, 실제 칩에서 T32
attach가 실패한다는 증거는 아니다. Cortex-M33은 디버그 구성 요소를 코어와
NVIC에서 별도의 전원 도메인으로 구성할 수 있어, 디버그 접근 가능성은 대상
SoC의 구성에 따라 달라진다.
[Arm Cortex-M33 TRM, C1.1 Debug functionality](https://documentation-service.arm.com/static/5e7c93a616d2907d59407132)

이번 검증은 QEMU의 GDB 인터페이스로 수행했다. 실제 T32, 대상 칩의 전원·클럭
도메인, 디버그 인증 및 watchdog/reset 동작은 검증하지 않았다. 따라서 정확한
T32 상태와 attach 실패 원인은 실기기에서 CPUID 접근 오류와 디버그/전원 상태를
함께 관측해야 확인할 수 있다.

## 한 브랜치에서 두 상태 재실행

테스트 코드는 [중첩 IRQ runner](../../tools/qemu-fault-lab/nested.py),
[fault 주입 runner](../../tools/qemu-fault-lab/run.py),
[nested defconfig](../../build/configs/qemu-armv8m/fault_lab_nested/defconfig)에 있다.
두 상태 모두 `qemu-armv8m/fault_lab_nested` recipe를 사용한다.

기본 패치 제거 상태에서 `os/`의 `/opt/homebrew/bin/bash ./dbuild.sh menu`를
실행한다. 새 checkout이면 `qemu-armv8m` → `fault_lab_nested` →
`1. Build with Current Configuration`을 선택한다. 다른 설정이 남아 있으면
`5. Clean Build and Re-Configure`를 먼저 선택한다. 빌드 후 저장소 루트에서:

```bash
python3 tools/qemu-fault-lab/nested.py \
  --case all --repeat 3 --timeout 6 --patch-state patch-removed \
  --output build/qemu-fault-lab/my-removed-nested
python3 tools/qemu-fault-lab/run.py \
  --case all --timeout 10 \
  --output build/qemu-fault-lab/my-removed-explicit
```

패치 포함 상태는 저장소 루트에서 아래 파일을 적용한다. `--check`가 실패하면
적용하지 않는다. 이 파일이 수정하는 경로에 별도 미커밋 작업이 없는 checkout에서
실행한다.

```bash
git apply --check tools/qemu-fault-lab/patch-present.patch
git apply tools/qemu-fault-lab/patch-present.patch
```

`os/`의 빌드 메뉴에서 `5. Clean Build and Re-Configure`를 선택해
`qemu-armv8m` → `fault_lab_nested`를 다시 설정하고 빌드한다. 재설정은 보조
IRQ 스택의 설정까지 반영하기 위해 필요하다. 빌드 후 저장소 루트에서:

```bash
python3 tools/qemu-fault-lab/nested.py \
  --case all --repeat 3 --timeout 6 --patch-state patch-present \
  --output build/qemu-fault-lab/my-present-nested
python3 tools/qemu-fault-lab/run.py \
  --case all --timeout 10 \
  --output build/qemu-fault-lab/my-present-explicit
```

검증 후 같은 patch를 역적용하면 원래 패치 제거 소스로 돌아온다.

```bash
git apply --reverse --check tools/qemu-fault-lab/patch-present.patch
git apply --reverse tools/qemu-fault-lab/patch-present.patch
```

역적용 후에는 다시 `5. Clean Build and Re-Configure`로 빌드한다. 설정과 ELF가
이전 패치 상태로 남아 있는 동안에는 runner를 실행하지 않는다.
테스트 1–6은 어느 상태든 같은 TASH 명령으로 선택한다.

## 통합 checkout 재검증

동일한 통합 checkout에서 patch 파일 적용 전후를 각각 clean build하고,
숫자 선택자 1–6을 각 상태에서 1회씩 새 QEMU 게스트로 실행했다. 총 12회 모두
위 기대 행렬과 일치했다. 양쪽 빌드의 partition/size 검증도 통과했다.
[재검증 요약 및 ELF 해시](../evidence/qemu-fault-lab/20261009/consolidation-validation.json),
[패치 제거 1–3](../evidence/qemu-fault-lab/20261009/consolidated-recheck/patch-removed/nested/results.json),
[패치 제거 4–6](../evidence/qemu-fault-lab/20261009/consolidated-recheck/patch-removed/explicit/results.json),
[패치 포함 1–3](../evidence/qemu-fault-lab/20261009/consolidated-recheck/patch-present/nested/results.json),
[패치 포함 4–6](../evidence/qemu-fault-lab/20261009/consolidated-recheck/patch-present/explicit/results.json).

재검증의 `git_head`는 테스트 코드가 동일한 통합 초안 커밋을 가리킨다. 이후
로그와 이 문서를 추가해 두 번째 커밋에 함께 정리했으므로 최종 커밋 ID와 다르다.
기존 관측 36회와 통합 재검증 12회는 각각의 원래 시각·해시를 유지했다.

## 검증 범위

두 펌웨어 모두 빌드 및 partition/size 검증을 통과했다. 사용한 ELF SHA-256은
패치 제거 `1275dca8fa515297ad8ad750db4da5507ae87f03e4f29084d72cb19b2d26c4f3`,
패치 포함 `85e32d8768e0ca265062956c13cb4db752296495888d34f23313125197e04aad`이다.
하네스는 QEMU 전용 IRQ hook과 테스트 PSP를 추가하므로, 실제 Wi-Fi stress나
실기기 인터럽트 빈도를 대신하지 않는다. 패치 제거용 실험은 별도 worktree에서
진행했으며 `codex/qemu-armv8m-kernel-tc`의 기존 변경은 건드리지 않았다.

결과 JSON, 주요 로그, effective config와 소스 스냅샷은
`docs/evidence/qemu-fault-lab/20261009/`에 포함되어 원격에서도 읽을 수 있다.
원래 실행의 ELF와 전체 원시 증거는 로컬
`/Users/seokhun/Dev/TizenRT/cleanup-records/20261009-qemu-fault-consolidation/raw-evidence/`에
보존했다. 이전 두 비교 worktree와 브랜치는 통합 브랜치 push 확인 후 정리한다.
