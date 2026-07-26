# `bk7239n`과 `qemu-armv8m` 4개 recipe의 defconfig 비교

## 비교 범위와 방법

현재 checkout의 다음 네 recipe를 같은 이름끼리 비교한다.

| recipe | BK7239N | QEMU ARMv8-M |
| --- | --- | --- |
| `hello` | [`bk7239n/hello/defconfig`](../../build/configs/bk7239n/hello/defconfig) | [`qemu-armv8m/hello/defconfig`](../../build/configs/qemu-armv8m/hello/defconfig) |
| `loadable_all` | [`bk7239n/loadable_all/defconfig`](../../build/configs/bk7239n/loadable_all/defconfig) | [`qemu-armv8m/loadable_all/defconfig`](../../build/configs/qemu-armv8m/loadable_all/defconfig) |
| `loadable_apps` | [`bk7239n/loadable_apps/defconfig`](../../build/configs/bk7239n/loadable_apps/defconfig) | [`qemu-armv8m/loadable_apps/defconfig`](../../build/configs/qemu-armv8m/loadable_apps/defconfig) |
| `xip_all` | [`bk7239n/xip_all/defconfig`](../../build/configs/bk7239n/xip_all/defconfig) | [`qemu-armv8m/xip_all/defconfig`](../../build/configs/qemu-armv8m/xip_all/defconfig) |

수치는 2026-07-26 현재 파일에서 다시 계산했다.

- 물리 줄 비교는 주석, 비활성 설정(`# CONFIG_X is not set`), 빈 줄을 포함한
  `diff -u` 기준이다.
- 설정 항목 비교는 `CONFIG_X=value`와
  `# CONFIG_X is not set`을 각각 하나의 상태로 정규화한다.
- `common diff`는 두 파일에 모두 존재하지만 값이 다른 심볼 수다.
- `BK-only`와 `QEMU-only`는 해당 파일에만 등장하는 심볼 수다.

## 전체 비교 요약

| recipe | BK/QEMU 물리 줄 | BK/QEMU 설정 항목 | common | same | common diff | BK-only | QEMU-only |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `hello` | 2,281 / 594 | 1,485 / 423 | 330 | 300 | 30 | 1,155 | 93 |
| `loadable_all` | 2,420 / 620 | 1,608 / 465 | 394 | 360 | 34 | 1,214 | 71 |
| `loadable_apps` | 2,424 / 620 | 1,612 / 465 | 394 | 359 | 35 | 1,218 | 71 |
| `xip_all` | 2,405 / 615 | 1,593 / 460 | 359 | 315 | 44 | 1,234 | 101 |

설정 순서와 보드별 주석 블록이 다르므로 네 비교 모두 물리적인 `diff -u`
결과는 사실상 전체 파일 hunk다. 따라서 아래의 공통 심볼 차이 목록은
의미 있는 정규화 결과이고, 비활성 심볼의 위치와 모든 물리 라인 차이는
재현 명령의 `diff -u`가 권위 있는 결과다.

## 공통 심볼 값이 다른 항목

`n`은 `# CONFIG_X is not set`을 의미한다. 줄 번호는 현재 defconfig의
원본 줄 번호다.

### `hello` — 30개

| 심볼 | BK7239N | QEMU ARMv8-M |
| --- | --- | --- |
| `CONFIG_ARCH_BOARD` | 236: `"bk7239n"` | 119: `"qemu-armv8m"` |
| `CONFIG_ARCH_CHIP` | 70: `"armino"` | 41: `"qemu-armv8m"` |
| `CONFIG_BOARD_BUILD_DATE` | 27: `"200204"` | 20: `"260611"` |
| `CONFIG_BOARD_LOOPSPERMSEC` | 195: `22222` | 93: `20000` |
| `CONFIG_BOOT_RUNFROMFLASH` | 212: `n` | 105: `y` |
| `CONFIG_CLOCK_MONOTONIC` | 1106: `n` | 142: `y` |
| `CONFIG_DRIVERS_OS_API_TEST` | 1260: `n` | 223: `y` |
| `CONFIG_ENABLE_PS` | 2215: `n` | 573: `y` |
| `CONFIG_EXAMPLES_TESTCASE` | 2136: `n` | 452: `y` |
| `CONFIG_FLASH_PART_NAME` | 1055: `"bl1,sys_its,sys_ps,ss,kernel,kernel,userfs,reserved,easy_flash,bootparam,"` | 361: `"kernel,userfs,"` |
| `CONFIG_FLASH_PART_SIZE` | 1053: `"92,8,8,404,1856,1856,2048,10080,24,8,"` | 359: `"640,3456,"` |
| `CONFIG_FLASH_PART_TYPE` | 1054: `"none,none,none,none,kernel,kernel,smartfs,none,none,bootparam,"` | 360: `"kernel,smartfs,"` |
| `CONFIG_FLASH_SIZE` | 1041: `16777216` | 356: `4194304` |
| `CONFIG_FLASH_START_ADDR` | 1040: `0x0` | 355: `0x80c00000` |
| `CONFIG_FS_AUTOMOUNT_PROCFS` | 1711: `n` | 588: `y` |
| `CONFIG_INTELHEX_BINARY` | 33: `y` | 25: `n` |
| `CONFIG_NETDEVICES` | 1261: `n` | 224: `y` |
| `CONFIG_NET_DEFAULT_TCP_RECVMBOX_SIZE` | 1508: `54` | 304: `64` |
| `CONFIG_NET_MEM_SIZE` | 1519: `46080` | 308: `153600` |
| `CONFIG_NET_NETMGR_ZEROCOPY` | 1664: `y` | 330: `n` |
| `CONFIG_NET_PING_CMD_ICOUNT` | 2205: `5` | 562: `3` |
| `CONFIG_NET_TCPIP_THREAD_PRIO` | 1528: `105` | 310: `110` |
| `CONFIG_NET_TCPIP_THREAD_STACKSIZE` | 1529: `4096` | 311: `8192` |
| `CONFIG_RAMMTD` | 1751: `n` | 372: `y` |
| `CONFIG_RAM_KREGIONx_SIZE` | 224: `"1835008,655360,"` | 111: `"12582912,524288,"` |
| `CONFIG_RAM_KREGIONx_START` | 223: `"0x70000000,0x28000000,"` | 110: `"0x80000000,0x10380000,"` |
| `CONFIG_REG_STACK_OVERFLOW_PROTECTION` | 111: `n` | 62: `y` |
| `CONFIG_SCHED_CHILD_STATUS` | 1126: `n` | 582: `y` |
| `CONFIG_STACK_OVERFLOW_PROTECTION_DISABLE` | 113: `y` | 63: `n` |
| `CONFIG_UART0_TXBUFSIZE` | 1315: `1024` | 238: `4096` |

### `loadable_all` — 34개

```text
CONFIG_APP1_BIN_VER: BK=190412, QEMU=260611
CONFIG_APP2_BIN_VER: BK=190412, QEMU=260611
CONFIG_ARCH_BOARD: BK="bk7239n", QEMU="qemu-armv8m"
CONFIG_ARCH_CHIP: BK="armino", QEMU="qemu-armv8m"
CONFIG_BOARD_BUILD_DATE: BK="200204", QEMU="260611"
CONFIG_BOARD_LOOPSPERMSEC: BK=22222, QEMU=20000
CONFIG_COMMON_BINARY_VERSION: BK="200204", QEMU=260611
CONFIG_DEBUG_VERBOSE: BK=n, QEMU=y
CONFIG_ENABLE_PS: BK=n, QEMU=y
CONFIG_EXAMPLES_TESTCASE_KERNEL: BK=n, QEMU=y
CONFIG_FLASH_PART_NAME: BK="bl1,sys_its,sys_ps,ss,kernel,common,app1,app2,kernel,common,app1,app2,userfs,reserved,easyflash,bootparam,", QEMU="kernel,common,app1,app2,kernel,common,app1,app2,reserved,bootparam,"
CONFIG_FLASH_PART_SIZE: BK="92,8,8,404,1624,1024,384,128,1624,1024,384,128,512,9008,24,8,", QEMU="640,768,384,128,640,768,384,128,248,8,"
CONFIG_FLASH_PART_TYPE: BK="none,none,none,none,kernel,bin,bin,bin,kernel,bin,bin,bin,smartfs,none,none,bootparam,", QEMU="kernel,bin,bin,bin,kernel,bin,bin,bin,none,bootparam,"
CONFIG_FLASH_SIZE: BK=16777216, QEMU=4194304
CONFIG_FLASH_START_ADDR: BK=0x0, QEMU=0x80c00000
CONFIG_FS_AUTOMOUNT_PROCFS: BK=n, QEMU=y
CONFIG_FS_TMPFS: BK=n, QEMU=y
CONFIG_INTELHEX_BINARY: BK=y, QEMU=n
CONFIG_MM_ASSERT_ON_FAIL: BK=n, QEMU=y
CONFIG_NETDEVICES: BK=n, QEMU=y
CONFIG_NET_DEFAULT_TCP_RECVMBOX_SIZE: BK=54, QEMU=64
CONFIG_NET_MEM_SIZE: BK=51200, QEMU=153600
CONFIG_NET_NETMGR_ZEROCOPY: BK=y, QEMU=n
CONFIG_NET_PING_CMD_ICOUNT: BK=5, QEMU=3
CONFIG_NET_TCPIP_THREAD_PRIO: BK=107, QEMU=110
CONFIG_NET_TCPIP_THREAD_STACKSIZE: BK=4096, QEMU=8192
CONFIG_RAMMTD: BK=n, QEMU=y
CONFIG_RAM_KREGIONx_HEAP_INDEX: BK="0,1,2,", QEMU="0,2,1,"
CONFIG_RAM_KREGIONx_SIZE: BK="4980736,524288,3145728,", QEMU="4194304,8388608,524288,"
CONFIG_RAM_KREGIONx_START: BK="0x70040000,0x28000000,0x70500000,", QEMU="0x80000000,0x80400000,0x10380000,"
CONFIG_RAM_SIZE: BK=0, QEMU=8388608
CONFIG_RAM_START: BK=0x0, QEMU=0x80400000
CONFIG_SCHED_CHILD_STATUS: BK=n, QEMU=y
CONFIG_SYS_RESERVED: BK=11, QEMU=8
```

`loadable_all`의 QEMU-only 71개는 QEMU board/chip, LAN9118용 generic
Ethernet/netdev, RAMMTD erase, tmpfs, semaphore holder, network/ITC/kernel
testcase와 UART buffer를 포함한다. 특히
`CONFIG_RAM_KREGIONx_HEAP_INDEX`가 BK의 `0,1,2`와 QEMU의 `0,2,1`로
다르므로, 세 번째 heap이 실행 이미지 heap인지 확인할 때 index와
주소/크기를 함께 읽어야 한다.

### `loadable_apps` — 35개

`loadable_apps`는 `loadable_all`의 공통 차이에서
`CONFIG_MM_ASSERT_ON_FAIL`이 빠지고 다음 두 항목이 추가된다.

```text
CONFIG_APP1_BIN_DYN_RAMSIZE: BK=2048000, QEMU=2097152
CONFIG_BOOT_RUNFROMFLASH: BK=n, QEMU=y
```

나머지 33개는 `loadable_all`과 동일하다. 이 recipe의 핵심 추가 차이는
app1 동적 RAM budget과 flash boot 경로이며, QEMU package loader를
검증할 때 `app1`의 loadable image 크기와 RAM budget을 함께 확인해야 한다.

### `xip_all` — 44개

```text
CONFIG_APP1_BIN_VER: BK=190412, QEMU=260611
CONFIG_ARCH_BOARD: BK="bk7239n", QEMU="qemu-armv8m"
CONFIG_ARCH_CHIP: BK="armino", QEMU="qemu-armv8m"
CONFIG_BINMGR_UPDATE_SAME_VERSION: BK=y, QEMU=n
CONFIG_BOARD_BUILD_DATE: BK="200204", QEMU="260611"
CONFIG_BOARD_LOOPSPERMSEC: BK=22222, QEMU=20000
CONFIG_BOOT_RUNFROMFLASH: BK=n, QEMU=y
CONFIG_COMMON_BINARY_VERSION: BK="200204", QEMU=260611
CONFIG_DEBUG_VERBOSE: BK=n, QEMU=y
CONFIG_DRIVERS_OS_API_TEST: BK=n, QEMU=y
CONFIG_ENABLE_PS: BK=n, QEMU=y
CONFIG_EXAMPLES_TESTCASE: BK=n, QEMU=y
CONFIG_FLASH_PART_NAME: BK="bl1,sys_its,sys_ps,ss,kernel,common,app1,resource,kernel,common,app1,resource,userfs,reserved,easyflash,bootparam,", QEMU="kernel,common,app1,reserved,kernel,common,app1,reserved,reserved,bootparam,"
CONFIG_FLASH_PART_SIZE: BK="92,8,8,404,1844,3400,1024,756,1844,3400,1024,756,512,1280,24,8,", QEMU="640,768,384,128,640,768,384,128,248,8,"
CONFIG_FLASH_PART_TYPE: BK="none,none,none,none,kernel,bin,bin,resource,kernel,bin,bin,resource,smartfs,none,none,bootparam,", QEMU="kernel,bin,bin,none,kernel,bin,bin,none,none,bootparam,"
CONFIG_FLASH_SIZE: BK=16777216, QEMU=4194304
CONFIG_FLASH_START_ADDR: BK=0x0, QEMU=0x80c00000
CONFIG_FLASH_VSTART_LOADABLE: BK=0x12236000, QEMU=0x80ca0000
CONFIG_FS_TMPFS_BLOCKSIZE: BK=1024, QEMU=512
CONFIG_FS_TMPFS_FILE_ALLOCGUARD: BK=1024, QEMU=512
CONFIG_FS_TMPFS_FILE_FREEGUARD: BK=2048, QEMU=1024
CONFIG_HEAP_INDEX_LOADED_APP: BK=0, QEMU=2
CONFIG_INTELHEX_BINARY: BK=y, QEMU=n
CONFIG_KMM_NHEAPS: BK=2, QEMU=3
CONFIG_KMM_REGIONS: BK=2, QEMU=3
CONFIG_NETDEVICES: BK=n, QEMU=y
CONFIG_NET_DEFAULT_TCP_RECVMBOX_SIZE: BK=54, QEMU=64
CONFIG_NET_MEM_SIZE: BK=51200, QEMU=153600
CONFIG_NET_NETMGR_ZEROCOPY: BK=y, QEMU=n
CONFIG_NET_PING_CMD_ICOUNT: BK=5, QEMU=3
CONFIG_NET_TCPIP_THREAD_PRIO: BK=107, QEMU=110
CONFIG_NET_TCPIP_THREAD_STACKSIZE: BK=4096, QEMU=8192
CONFIG_NFILE_DESCRIPTORS: BK=64, QEMU=32
CONFIG_PREALLOC_TIMERS: BK=8, QEMU=2
CONFIG_RAMMTD: BK=n, QEMU=y
CONFIG_RAM_KREGIONx_HEAP_INDEX: BK="0,1,", QEMU="0,2,1,"
CONFIG_RAM_KREGIONx_SIZE: BK="2621440,524288,", QEMU="4194304,8388608,524288,"
CONFIG_RAM_KREGIONx_START: BK="0x70000000,0x28000000,", QEMU="0x80000000,0x80400000,0x10380000,"
CONFIG_RAM_SIZE: BK=5767168, QEMU=8388608
CONFIG_RAM_START: BK=0x70280000, QEMU=0x80400000
CONFIG_SCHED_CHILD_STATUS: BK=n, QEMU=y
CONFIG_SCHED_LPWORKSTACKSIZE: BK=4096, QEMU=2048
CONFIG_SYS_RESERVED: BK=11, QEMU=8
CONFIG_WDOG_INTRESERVE: BK=4, QEMU=0
```

`xip_all`의 QEMU-only 101개에는 LAN9118 Ethernet/network command와
network testcase, ELF loadable buffer/dual-slot,
`CONFIG_BINFMT_SECTION_UNIFIED_MEMORY`, QEMU kernel testcase와 ITC/kernel
testcase 세트가 포함된다. BK는 동일 버전 update를 허용하지만 QEMU는
`CONFIG_BINMGR_UPDATE_SAME_VERSION=n`이다. 따라서 QEMU A/B 검증은 버전
증가와 active slot 전환을 기준으로 한다.

## 차이의 의미와 메모리 레이아웃

### `hello`

QEMU hello는 RAM-backed flash를 `0x80c00000`부터 4 MiB 사용하며,
`kernel=640 KiB`, `userfs=3,456 KiB`로 나눈다. `CONFIG_RAMMTD=y`와
SmartFS 자동 mount를 이용하므로 `/mnt`에서 TASH/SmartFS를 확인할 수
있다. 두 보드 모두 IPv4/IPv6 네트워크를 켜지만 BK는 Wi-Fi/zero-copy
netmgr를, QEMU는 LAN9118 Ethernet/copy-mode netmgr를 사용한다. BK는
16 MiB 실제 flash partition, Intel HEX와 vendor PSRAM/SoC 설정을 가진다.

QEMU RAM heap은 `0x80000000/12 MiB`와 `0x10380000/512 KiB`이고 BK는
`0x70000000/1,835,008 bytes`와 `0x28000000/655,360 bytes`다. 두 config
모두 `CONFIG_RAM_MALLOC_PRIOR_INDEX=0`이다. `CONFIG_ENABLE_PS=y`는
QEMU TASH `ps` 검증을 위해 QEMU에서 켠 설정이다.

### `loadable_all`과 `loadable_apps`

두 recipe 모두 kernel/common/app1/app2를 두 set으로 패키징한다. QEMU는
RAMMTD 기반 4 MiB layout과 세 heap을 사용하고, BK는 실제 16 MiB flash와
PSRAM/보드 heap을 사용한다. QEMU의 세 heap은 다음과 같다.

| heap index | 시작 주소 | 크기 | 용도 해석 |
| ---: | ---: | ---: | --- |
| 0 | `0x80000000` | 4 MiB | kernel/main heap 영역 |
| 2 | `0x80400000` | 8 MiB | app/loadable 영역 |
| 1 | `0x10380000` | 512 KiB | 보조 SRAM 영역 |

이 순서는 `CONFIG_RAM_KREGIONx_HEAP_INDEX="0,2,1,"`와 일치한다.
`loadable_apps`는 app1 dynamic RAM size만 추가로 BK `2,048,000` bytes와
QEMU `2,097,152` bytes가 다르다.

### `xip_all`

QEMU는 `CONFIG_KMM_REGIONS=3`, `CONFIG_KMM_NHEAPS=3`과
`CONFIG_HEAP_INDEX_LOADED_APP=2`를 사용해 SRAM app heap을 별도로 둔다.
또한 package의 XIP/loadable 영역을 `0x80ca0000`에서 시작한다. BK는
두 heap과 실제 flash resource partition을 사용한다. QEMU의 `xip_all`은
현재 Binary Manager의 persistent A/B state와 dual-slot 경로를 검증하는
목적에 맞춰져 있다.

### 의도적으로 유지한 하드웨어 차이

BK-only 설정 1,155~1,234개는 vendor HAL, 실제 flash/PSRAM, TrustZone,
Wi-Fi/BLE, GPIO/SPI/I2C/PWM 같은 hardware 기능이다. QEMU-only 설정
71~101개는 QEMU board glue, LAN9118 Ethernet, RAMMTD, tmpfs, test provider,
network/ITC/kernel testcase와 loader buffer다. 이 설정을 숫자만 맞추는
것은 안전하지 않으며, 메모리 주소·linker script·board source·package
layout을 함께 변경해야 한다.

## 이번 변경에서 확인한 정렬 항목

QEMU hello는 다음을 BK hello 기준으로 맞췄다.

- `CONFIG_BOARD_ASSERT_AUTORESET=y`
- `CONFIG_RAM_MALLOC_PRIOR_INDEX=0`
- TASH: `CONFIG_TASH=y`, task priority `125`, command stack `4096`,
  command priority `100`, `CONFIG_TASH_REBOOT=y`
- SmartFS: `CONFIG_FS_SMARTFS=y`, RAMMTD partition, sector `1024`,
  wear-level/CRC16/journaling 옵션
- `CONFIG_FILESYSTEM_TEST=y`, `CONFIG_SMARTFS_TEST=y`

QEMU hello에서는 `CONFIG_ENABLE_PS=y`를 유지해 `help`, `ps`, `mount`,
`ls /mnt`, `smartfs_test`를 터미널에서 실행할 수 있게 했다. loadable/xip
recipe는 hello와 목적이 달라 Binary Manager, package, A/B 설정이 더 많다.

네 QEMU recipe는 공통으로 LAN9118, IPv4/IPv6, DHCP, DNS, TCP/UDP/raw,
IGMP/MLD, `ifconfig`/`ifup`/`ifdown`/`ping`/`ping6`/`netmon`/
`net_stats`/`netdb`, network testcase를 켠다. QEMU LAN9118 driver는
lwIP pbuf를 board driver로 복사하는 방식이므로
`CONFIG_NET_NETMGR_ZEROCOPY=n`을 의도적으로 유지한다. Wi-Fi/BLE,
DHCP server, TFTP, webclient, iperf, TLS는 이번 QEMU Ethernet 범위가 아니다.

## 검증 결과

2026-07-26에 현재 checkout에서 각 recipe를 `./os/dbuild.sh 5`와 동일한
`distclean → reconfigure → build` 경로로 clean
build한 뒤 configure/build와 QEMU runner를 순서대로 실행했다.

| recipe | clean build | Network TC | Kernel TC | 결과 |
| --- | --- | --- | --- | --- |
| `hello` | kernel `513,492 / 655,360` PASS | `161/0` | `459/0` | PASS |
| `loadable_all` | kernel/common/app1/app2 PASS | `161/0` | `447/0` | PASS |
| `loadable_apps` | kernel/common/app1/app2 PASS | `161/0` | `447/0` | PASS |
| `xip_all` | kernel/common/app1 A/B PASS | `161/0` | `447/0` | PASS |

추가 negative/A-B 검증도 완료했다.

| 시나리오 | 입력 | 기대 결과 | 결과 |
| --- | --- | --- | --- |
| package corruption | `xip_all`의 `common` header byte 변조 | `Invalid Header data`, success marker 없음 | expected-rejection |
| omitted package | `xip_all`에서 `common` package 생략 | `Invalid Header data`, success marker 없음 | expected-rejection |
| package corruption | `loadable_all`의 `app1` header byte 변조 | `Invalid Header data`, success marker 없음 | expected-rejection |
| alternate slot | 손상된 A-slot app1, 정상 B-slot app1 | BP version 1→2, set B 재부팅, network `161/0`, kernel `447/0` | PASS, active slot 1 |

alternate-slot 로그에서 `binary_manager_recover_bootparam_set`이 set B
validation을 통과하고 active slot 1로 전환한 뒤, 다음 boot에서 common/app1/
app2 header validation과 kernel testcase가 모두 통과했다.

## 재현 명령

### 네 recipe의 물리 line-by-line 비교

```bash
cd /Volumes/T7/Dev/TizenRT/codex/qemu-armv8m-kernel-tc

for recipe in hello loadable_all loadable_apps xip_all; do
  diff -u \
    --label "bk7239n/${recipe}/defconfig" \
    --label "qemu-armv8m/${recipe}/defconfig" \
    "build/configs/bk7239n/${recipe}/defconfig" \
    "build/configs/qemu-armv8m/${recipe}/defconfig" \
    || true
done
```

`diff -u`의 종료 코드 `1`은 차이가 있다는 뜻이므로 네 recipe를 일괄
확인할 때 `|| true`를 붙인다. 심볼별 동일/차이 수를 다시 계산하려면
`CONFIG_X=value`와 `# CONFIG_X is not set`을 정규화하는 parser를 사용해야
하며, 단순 `diff` 줄 수를 심볼 수로 해석하면 안 된다.

### clean build와 runner

```bash
cd os
./dbuild.sh distclean qemu-armv8m hello build
cd ..
python3 .github/scripts/qemu-armv8m-kernel-tc.py \
  --config hello --timeout 1200
```

`hello`에서는 runner 전에 QEMU TASH에서 `help`, `ps`, `mount`,
`ls /mnt`, `smartfs_test 4 /mnt/test 4096 1 y`를 실행한다. loadable/xip의
package corruption, omitted package, alternate-slot 시나리오는 runner의
`--common`, `--app1`, `--omit-common`, `--state-image` 옵션과 예상 reject
marker를 함께 사용해 재현한다.
