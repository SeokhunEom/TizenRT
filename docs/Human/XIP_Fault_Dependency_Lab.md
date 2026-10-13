# XIP 접근 불가 시 fault 경로 비교

2026-10-13. `codex/qemu-armv8m-fault-tests`의 **시험 전용** 기능이다.
`CONFIG_ARMV8M_XIP_FAULT_LAB`은 기본 off다.

## 이 시험이 확인하는 것

동일한 이미지에서 flash에 대한 MPU 접근 권한을 없앤 뒤 RAM의 `UDF`로
HardFault를 발생시킨다. handler와 그 종속 코드/데이터의 배치에 따라
복구 경로가 실행 가능한지 비교한다. 실제 flash 쓰기/지우기는 수행하지 않는다.

**Realtek SPIC user mode, flash busy, 버스 무응답을 구현한 시험이 아니다.**
MPU는 접근에 오류를 반환한다. 실제 XIP bus stall은 접근 자체가 완료되지
않을 수 있으므로 이 시험의 lockup과 구분해야 한다. T32 attach 실패,
`running(core power down)`, cache miss 타이밍도 QEMU에서 검증하지 않는다.
운영 fault handler를 고친 것이 아니라 독립된 최소 시험 handler를 비교한다.

| 명령 | 접근 조건 / handler | 기대 관측 |
| --- | --- | --- |
| `xiplab` | 도움말만 출력 | 상태 변경 없음 |
| `xiplab 0` | flash 허용, flash의 handler 진입 | HardFault 진단 보존 → reset → TASH |
| `xiplab 1` | flash 차단, 0번과 같은 flash handler | 첫 명령어 fetch의 IACCVIOL → HardFault 중 재차 fault → lockup |
| `xiplab 2` | flash 차단, RAM handler가 flash 상수를 읽음 | handler 진입 후 DACCVIOL → lockup |
| `xiplab 3` | flash 차단, entry·capture·checksum·reset 모두 RAM | 진단 보존 → reset → TASH |

`0`은 정상 대조군이다. `1`과 동일한 flash entry가 접근 허용 상태에서는
동작하는지 확인한다. `2`의 상수 읽기는 assembly `ldr`이므로 컴파일러가
상수로 치환할 수 없다. `1/2`에는 시험용 무한 루프로 정지를 꾸미지 않는다.
QEMU는 architectural lockup에서 프로세스를 종료한다. 이는 보드의 전원이
꺼지는 것과 다르며, 실물은 watchdog/외부 reset 등으로 벗어날 수 있다.

## 구현의 범위

- TizenRT SMP off / flat / FPU off. QEMU `mps2-an505`와
  RTL8721CSM의 **TrustZone off** 시험 recipe만 지원한다. RTL 시험은 KM4에서만
  실행하며 KM0와의 실제 flash 접근 동기화는 검증하지 않는다.
- 명령 실행 시에만 VTOR를 RAM의 시험 벡터로 바꾸고 MPU를 임시 재설정한다.
  PRIMASK로 일반 IRQ를 막고 RAM/필요한 MMIO만 허용한다. 정상 OS로 복귀하지
  않고 reset하므로 기존 MPU 설정을 복원하는 실험은 아니다.
- MPU CTRL은 `ENABLE | HFNMIENA = 3`, `PRIVDEFENA = 0`이다.
  HFNMIENA를 빠뜨리면 HardFault가 차단을 우회하여 잘못된 성공을 만들 수 있다.
- Cortex-M33의 PRIMASK가 설정된 상태에서 `UDF`는 HardFault로 escalation된다.
  RAM handler는 OS 공통 exception prologue를 우회하고 전용 1 KiB 스택을 사용한다.
- RTL 시험은 MPU 메모리 속성을 변경하기 전에 KM4 cache를 clean/disable한다.
  cache가 이미 채워져 있어서 통과하는 경우를 배제하기 위한 조치이며 실제 제품의
  cache/flash timing을 재현하는 설정이 아니다. reset까지 cache를 켜지 않는다.
- `g_xiplab_record`는 NOLOAD에 놓고 BSS 초기화와 heap에서 제외한다.
  checksum과 magic이 맞을 때만 다음 부팅에서 출력한다. QEMU warm reset의
  보존은 확인하지만 **실물 ROM/bootloader의 SRAM 초기화와 보존은 별도 확인**한다.
- 표준 `up_hardfault()`의 진단 출력, flash lock, SPIC mode 전환을 실행하지 않는다.
  이 시험 통과만으로 제품의 fault 경로 전체가 RAM에 있다고 판단하지 않는다.

## QEMU 실행

저장소의 `os/`에서 빌드한다. macOS에서는 Homebrew Bash를 사용한다.

```sh
cd os
/opt/homebrew/bin/bash ./dbuild.sh menu
```

`5. Clean Build and Re-Configure` → `qemu-armv8m` → `xip_fault_lab` →
`1. Build with Current Configuration` → 빌드 완료 후 `x. Exit`를 선택한다.
처음 설정하는 checkout에서는 보드/config부터 선택한다.

저장소 루트에서 자동 시험:

```sh
python3 tools/qemu-fault-lab/xip.py \
  --out build/qemu-fault-lab/xip-my-run --repeat 25
```

`--out`은 새 경로를 사용한다. `--cases 0,3`처럼 일부 선택도 가능하다.
runner는 다음 증거를 요구한다.

- ELF symbol로 RAM/flash 배치를 확인한다.
- `1/2`: 명령 실행 확인, 예상 fault 주소와 PC, IACCVIOL/DACCVIOL,
  HardFault priority에서 QEMU가 보고한 lockup. **timeout은 실패**다.
- `0/3`: 같은 QEMU 프로세스의 guest RESET 이벤트, checksum이 유효한
  기록, `exception=3`, 주입한 UDF의 PC, `CFSR=00010000`, `MPU=00000003`,
  재부팅 후 새 `xiplab` 명령의 응답을 확인한다.
- debugger 메모리 쓰기, host reset, 한 case 안에서의 QEMU 재실행은 하지 않는다.

기존 `faultlab 1..6`도 이 QEMU recipe에 함께 들어 있다.

```sh
python3 tools/qemu-fault-lab/recover.py \
  --out build/qemu-fault-lab/xip-existing-regression --repeat 3
```

## RTL8721CSM 보드 실행

빌드 메뉴에서 `rtl8721csm` → `xip_fault_lab`을 선택하고 clean build한다.
이 recipe는 기존 flat `hello`에서 파생하며 TrustZone과 그에 종속된 SE/HW TLS
옵션을 끈다. 다른 recipe의 보안 설정은 변경하지 않는다. TrustZone이 반드시
필요한 제품 이미지에 시험 설정을 그대로 적용하지 않는다.

생성한 이미지는 **해당 보드에서 원래 사용하는 flash 도구/부트로더 절차**로
다운로드한다. flash 주소·bootloader·partition은 임의로 바꾸지 않는다.
이 저장소의 `hello`와 같은 partition 설정을 사용하므로 제품의 기존 partition과
다르면 먼저 일치 여부를 확인한다. 설치 과정의 flashing과 `xiplab` 명령 자체의
동작은 별개다. `xiplab` 명령은 flash program/erase를 하지 않는다.

권장 순서는 각 명령을 새 부팅에서 `0 → 3 → 1 → 2`다.

1. TASH에서 `xiplab`으로 메뉴가 등록됐는지 확인한다.
2. `xiplab 0`: 정상 대조군. 재부팅과 TASH 복귀를 확인한다.
3. `xiplab 3`: 동일한 재부팅과, 보존되는 보드에서는
   `XIPLAB retained valid=1 case=3 stage=30 exception=3 ... mpu=00000003`를 확인한다.
4. `xiplab 1`: fault entry fetch 실패를 관측한다. 다음 시험 전에 외부 reset한다.
5. `xiplab 2`: RAM entry 이후 flash 데이터 읽기 실패를 관측한다. 외부 reset한다.

`1/2`는 의도적으로 lockup을 발생시킨다. watchdog이 이미 동작 중인 보드에서는
watchdog reset으로 보일 수 있다. 출력이 없다는 이유만으로 XIP hang으로 분류하지
말고 reset 원인, fault 상태, PC를 함께 남긴다. 이 작업에서는 실물 보드를
실행하지 않았으므로 실물 결과는 **미검증**이다.

실행 전 출력의 `handler`, `dependency`, `trigger`, `record` 주소와 같은 빌드의
ELF/MAP을 보존한다. 다음 symbol의 배치를 확인할 수 있다.

```sh
arm-none-eabi-nm -n build/output/bin/tinyara.axf | \
  rg 'xiplab_(flash_entry|ram_entry|capture|checksum|arm|region|trigger|read_dependency|dependency_load)|g_xiplab_(flash_word|record|vectors|stack)'
```

보드에서는 flash entry/word가 remapped flash `0x0e......`, RAM 경로가
`0x100.....`에 있어야 한다. 명령 자체도 일부 주요 주소/MPU/privilege를 검사해
지원하지 않는 배치면 주입 전에 거부한다.

## T32에서 남길 관측

가능하면 시험 전에 연결하고 **같은 ELF의 심벌만 로드**한다. 관측을 위해
ELF를 다시 다운로드하면 RAM 기록과 실행 상태가 바뀐다. 자동 flash 메모리 창,
software breakpoint나 source window의 flash 읽기도 원인과 구분한다.

| 항목 | 위치 / 기대 |
| --- | --- |
| `g_xiplab_record.stage` | `10`: 차단 후 UDF 직전, `20`: RAM entry 진입, `22`: flash 상수 읽기 직전, `30`: 진단 commit/reset 요청 단계 |
| 1번 PC | `xiplab_flash_entry` 부근, `CFSR.IACCVIOL` 확인 |
| 2번 PC / fault 주소 | `xiplab_dependency_load` / `g_xiplab_flash_word`, `CFSR.DACCVIOL`와 MMFAR 확인 |
| CFSR / HFSR / MMFAR | `0xe000ed28` / `0xe000ed2c` / `0xe000ed34` |
| VTOR / MPU CTRL | `0xe000ed08` / `0xe000ed94`; 실행 중 RAM vector / `3` |
| Debug 상태 | attach 가능, halt 가능, RAM 읽기 가능을 각각 기록 |

2번 RAM 기록의 CFSR은 **상수 접근 이전 최초 UDF** 값이다. 최종 DACCVIOL은
하드웨어 CFSR/MMFAR에서 확인한다. 1번은 handler 자체가 실행되지 않아 유효한
최종 진단 기록이 없고, 2번도 commit 전에 정지한다. 그 부재는 예상된 차이다.
자동 재부팅하는 `0/3`의 관측 시간은 짧으므로 연결 실패만으로 bus hang으로
판단하지 않는다. 기록이 다음 부팅에서 사라지면 bootloader retention부터 확인한다.

## 근거와 결과

- [QEMU MPS2/MPS3 모델 한계](https://www.qemu.org/docs/master/system/arm/mps2.html)
- [Arm CMSIS Armv8-M MPU 구성](https://github.com/ARM-software/CMSIS_5/blob/develop/CMSIS/Core/Include/mpu_armv8.h)
- [Lauterbach: processor stall과 break 요청 무응답](https://support.lauterbach.com/kb/articles/arm-break-command-returns-emulation-running)
- [기존 6개 fault 복구 검증](QEMU_ARMv8M_Fault_Recovery_Results.md)

최종 실행 결과와 빌드 증거는 같은 커밋의
`docs/evidence/qemu-fault-lab/20261013/xip/`에 보존한다.


### 이번 커밋에서 확인한 결과

| 검증 | 결과 |
| --- | --- |
| QEMU `xiplab 0/1/2/3` | 각각 25/25, 총 100/100. 1/2는 예상 lockup 재현 성공 |
| 기존 QEMU `faultlab 1..6` | 각각 3/3, 총 18/18 복귀/복구 성공 |
| 최종 QEMU 이미지 network / kernel | 161/0, 459/0 |
| RTL8721CSM `xip_fault_lab` | ELF·이미지 빌드, partition/size/header 검사 성공 |
| RTL ELF 배치 | RAM 함수/호출 대상과 flash entry/상수, BSS 밖 기록 위치 확인 |
| 실물 보드 실행 / T32 | 미실행·미검증 |

[결과 요약과 원본 증거](../evidence/qemu-fault-lab/20261013/xip/README.md)를 참고한다.
보드용 ELF/이미지의 이번 로컬 사본은 `build/qemu-fault-lab/rtl-xip-firmware/`에
보존했으며 Git에는 소스·설정·문서·검증 증거를 포함한다.
