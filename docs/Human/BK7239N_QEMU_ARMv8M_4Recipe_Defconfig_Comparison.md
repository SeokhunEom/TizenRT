# BK7239N과 QEMU ARMv8-M 네 recipe defconfig 비교

이 문서는 사람이 `./dbuild.sh menu`에서 recipe를 선택할 때 BK7239N과
QEMU ARMv8-M 설정의 차이를 빠르게 판단하기 위한 문서다. QEMU 설정을
BK7239N에 숫자만 맞추는 것은 안전하지 않다. QEMU는 MPS2-AN505의
LAN9118 Ethernet, RAM-backed flash, SRAM multiheap을 사용하고, BK7239N은
실제 flash/PSRAM과 vendor peripheral을 사용한다.

## 비교 대상

| recipe | BK7239N | QEMU ARMv8-M | 목적 |
| --- | --- | --- | --- |
| `hello` | [`bk7239n/hello/defconfig`](../../build/configs/bk7239n/hello/defconfig) | [`qemu-armv8m/hello/defconfig`](../../build/configs/qemu-armv8m/hello/defconfig) | flat 커널, TASH, SmartFS, 기본 네트워크 |
| `loadable_all` | [`bk7239n/loadable_all/defconfig`](../../build/configs/bk7239n/loadable_all/defconfig) | [`qemu-armv8m/loadable_all/defconfig`](../../build/configs/qemu-armv8m/loadable_all/defconfig) | kernel/common/app1/app2 패키지와 두 package set |
| `loadable_apps` | [`bk7239n/loadable_apps/defconfig`](../../build/configs/bk7239n/loadable_apps/defconfig) | [`qemu-armv8m/loadable_apps/defconfig`](../../build/configs/qemu-armv8m/loadable_apps/defconfig) | loadable application 중심 빌드 |
| `xip_all` | [`bk7239n/xip_all/defconfig`](../../build/configs/bk7239n/xip_all/defconfig) | [`qemu-armv8m/xip_all/defconfig`](../../build/configs/qemu-armv8m/xip_all/defconfig) | XIP app, Binary Manager, persistent A/B |

QEMU recipe 설명과 사람이 메뉴에서 빌드·실행하는 절차는
[QEMU ARMv8-M 터미널 가이드](QEMU_ARMv8M_Terminal_Guide.md)를 함께 본다.
정규화된 전체 심볼 목록은 [AI 상세 비교 문서](../AI/BK7239N_QEMU_ARMv8M_Hello_Defconfig_Comparison.md)에 있다.

## 한눈에 보는 차이

현재 defconfig의 주석·빈 줄을 포함한 물리 줄 수와 심볼을 정규화한 결과다.
물리 줄 수는 보드 주석과 설정 순서가 달라서 의미 있는 유사도 지표가
아니다. 실제 설정 판단에는 `same`, `common diff`, `BK-only`, `QEMU-only`를
사용한다.

| recipe | BK 줄 / 심볼 | QEMU 줄 / 심볼 | 공통 심볼 | 같은 값 | 공통 값 차이 | BK-only | QEMU-only |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `hello` | 2,281 / 1,485 | 594 / 423 | 330 | 300 | 30 | 1,155 | 93 |
| `loadable_all` | 2,420 / 1,608 | 620 / 465 | 394 | 360 | 34 | 1,214 | 71 |
| `loadable_apps` | 2,424 / 1,612 | 620 / 465 | 394 | 359 | 35 | 1,218 | 71 |
| `xip_all` | 2,405 / 1,593 | 615 / 460 | 359 | 315 | 44 | 1,234 | 101 |

## 네 recipe에 공통으로 적용되는 QEMU 방향

QEMU 네 recipe는 다음 계약을 공유한다.

- `CONFIG_ARCH_BOARD="qemu-armv8m"`, `CONFIG_ARCH_CHIP="qemu-armv8m"`
- RAM-backed flash 시작 주소 `0x80c00000`, 총 크기 4 MiB
- LAN9118 Ethernet, IPv4/IPv6, DHCP, DNS, TCP/UDP와 TASH 네트워크 명령
- `CONFIG_NET_NETMGR_ZEROCOPY=n`: QEMU Ethernet은 BK7239N Wi-Fi 경로와
  다른 copy-mode netmgr 경계를 사용한다.
- `CONFIG_RAMMTD=y`: QEMU에서 flash partition을 RAM으로 재현한다.
- `CONFIG_EXAMPLES_TESTCASE=y`, kernel/network testcase와 `CONFIG_TASH=y`
- `CONFIG_BOARD_ASSERT_AUTORESET=y`, `CONFIG_RAM_MALLOC_PRIOR_INDEX=0`
- `CONFIG_ENABLE_PS=y`: QEMU TASH에서 process 상태 확인을 가능하게 한다.

BK7239N의 `CONFIG_ENABLE_PS`, vendor Wi-Fi/LWNL, 실제 flash partition을
QEMU에 그대로 복사하는 것이 목표는 아니다. QEMU의 동등한 사용성은
LAN9118 board glue, RAMMTD, QEMU linker layout, runner가 함께 제공한다.

## recipe별 핵심 차이

### `hello`: flat 커널과 TASH/SmartFS

| 항목 | BK7239N | QEMU ARMv8-M | 의미 |
| --- | --- | --- | --- |
| 보드/칩 | `bk7239n` / `armino` | `qemu-armv8m` / `qemu-armv8m` | SoC와 QEMU architecture 경계 |
| flash | 16 MiB, `0x0` | 4 MiB, `0x80c00000` | 실제 flash와 RAM-backed flash 차이 |
| partition | vendor boot/system/kernel/userfs | `kernel=640 KiB`, `userfs=3456 KiB` | QEMU는 kernel+SmartFS 최소 layout |
| heap regions | `0x70000000/1835008`, `0x28000000/655360` | `0x80000000/12582912`, `0x10380000/524288` | PSRAM/실제 SRAM 대 QEMU RAM/SSRAM |
| monotonic clock | `CONFIG_CLOCK_MONOTONIC=n` | `y` | QEMU timer 구현이 monotonic clock 제공 |
| overflow | register 보호 `n`, disable `y` | register 보호 `y`, disable `n` | architecture별 stack 보호 방식 |
| 네트워크 | netmgr zero-copy와 vendor 경로 | LAN9118 Ethernet copy-mode | 기능 이름은 같아도 device path가 다름 |
| filesystem | 실제 flash 기반 vendor layout | RAMMTD + SmartFS auto-mount | QEMU 재부팅 시 RAM flash가 초기화될 수 있음 |

QEMU hello에서 사람이 확인할 기본 명령은 다음과 같다.

```text
help
ps
mount
ls /mnt
smartfs_test 4 /mnt/test 4096 1 y
ifconfig eth0
network_tc
kernel_tc
```

### `loadable_all`: 두 package set과 세 heap

두 보드 모두 `kernel/common/app1/app2`를 두 set으로 패키징하지만, package
partition의 주소와 보드 boot metadata는 다르다.

| 항목 | BK7239N | QEMU ARMv8-M |
| --- | --- | --- |
| flash partition | vendor boot/system 영역 포함, 16 MiB | `kernel,common,app1,app2`를 두 set으로 4 MiB에 배치 |
| `RAM_KREGIONx_START` | `0x70040000,0x28000000,0x70500000` | `0x80000000,0x80400000,0x10380000` |
| `RAM_KREGIONx_SIZE` | `4980736,524288,3145728` | `4194304,8388608,524288` |
| `RAM_KREGIONx_HEAP_INDEX` | `0,1,2` | `0,2,1` |
| QEMU 해석 | vendor heap과 PSRAM | heap 0 main RAM, heap 2 loaded app, heap 1 보조 SRAM |
| QEMU 전용 | 없음 | RAMMTD, tmpfs/procfs, LAN9118, loader/kernel testcase |

QEMU의 heap index는 주소 순서와 같지 않다. `0,2,1`을 보지 않고
`0,1,2`로 추정하면 loaded application을 잘못된 heap으로 해석하게 된다.

### `loadable_apps`: app1 동적 RAM 예산

`loadable_apps`는 `loadable_all`과 거의 같은 package 구조를 사용하지만,
app1의 동적 RAM 예산과 flash boot 경로가 별도 차이다.

| 항목 | BK7239N | QEMU ARMv8-M |
| --- | --- | --- |
| `CONFIG_APP1_BIN_DYN_RAMSIZE` | `2048000` | `2097152` |
| `CONFIG_BOOT_RUNFROMFLASH` | `n` | `y` |
| heap/package 차이 | vendor PSRAM/실제 flash 기준 | QEMU loader layout과 main-RAM/SRAM 기준 |

app1 package를 변경할 때는 ELF 크기만 확인하지 말고 이 RAM 예산,
`RAM_KREGIONx_HEAP_INDEX`, linker script의 application 영역을 함께 확인한다.

### `xip_all`: XIP와 loaded-app heap, A/B

| 항목 | BK7239N | QEMU ARMv8-M |
| --- | --- | --- |
| KMM regions/heaps | `2 / 2` | `3 / 3` |
| loaded app heap | `CONFIG_HEAP_INDEX_LOADED_APP=0` | `=2` |
| QEMU loaded-app region | 해당 없음 | `0x80400000`, 8 MiB heap 2 |
| XIP/loadable 시작 | `0x12236000` | `0x80ca0000` |
| RAM regions | `0x70000000/2621440`, `0x28000000/524288` | `0x80000000/4194304`, `0x80400000/8388608`, `0x10380000/524288` |
| update policy | `CONFIG_BINMGR_UPDATE_SAME_VERSION=y` | `n` |
| package role | 실제 flash resource partition 포함 | QEMU RAM flash dual-slot/A-B runner |

QEMU `xip_all`에서 package recovery를 확인할 때는 version을 증가시키고
active slot 전환과 boot parameter를 함께 관찰한다. 동일 version update를
허용하는 BK 정책을 QEMU에 복사하지 않은 것은 A/B 상태 전환을 명확히
검증하기 위해서다.

## 차이를 읽을 때의 우선순위

1. 보드/칩, flash 시작 주소와 partition을 확인한다.
2. `RAM_KREGIONx_START`, `SIZE`, `HEAP_INDEX`를 함께 확인한다.
3. flat/loadable/XIP의 loader와 linker script를 확인한다.
4. 네트워크는 QEMU LAN9118인지 BK vendor Wi-Fi/netmgr인지 확인한다.
5. 마지막으로 TASH, testcase, debug 옵션을 맞춘다.

BK-only 설정 대부분은 vendor HAL, TrustZone, 실제 flash/PSRAM, Wi-Fi/BLE,
GPIO/SPI/I2C/PWM이다. QEMU-only 설정 대부분은 board glue, LAN9118,
RAMMTD, tmpfs, loader, test provider다. 이 두 집합을 단순히 같은 값으로
만드는 것은 기능 추가가 아니라 잘못된 hardware assumption이 될 수 있다.

## 직접 line-by-line 비교

정확한 physical diff와 비활성 심볼까지 포함한 결과는 다음 명령으로 재현한다.

```bash
cd /Volumes/T7/Dev/TizenRT/codex/qemu-armv8m-kernel-tc
for recipe in hello loadable_all loadable_apps xip_all; do
  diff -u \
    "build/configs/bk7239n/$recipe/defconfig" \
    "build/configs/qemu-armv8m/$recipe/defconfig" \
    || true
done
```

심볼 값만 비교하려면 AI 문서의 정규화 기준을 사용한다. 한쪽에만 있는
심볼은 hardware/recipe boundary 후보이고, 공통 심볼의 값 차이는 먼저
위의 flash·heap·loader 차이와 연결해 판단한다.
