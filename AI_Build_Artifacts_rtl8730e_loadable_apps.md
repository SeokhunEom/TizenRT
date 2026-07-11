# rtl8730e/loadable_apps build artifacts

기준 시각은 로컬 파일 timestamp 기준 KST이다. 파일별 상세 timestamp 표는 최초
normal build 기준이며, full toolchain artifact 요약은 2026-07-11 최종 검증
결과 기준이다.

## Build result

- 대상: `rtl8730e/loadable_apps`
- 런북: `/Users/seokhun/Projects/TizenRT/AI_Build_Runbook.md`
- 작업 위치: `/Users/seokhun/Projects/TizenRT/master`
- Docker image: `tizenrt/tizenrt:2.0.0`
- 최종 결과: normal build PASS, full toolchain artifact build PASS
- 최종 로그: `os/build.log`

런북의 기본 빌드 명령은 repository root에서 바로 실행된다.

```sh
./dbuild.sh distclean configure rtl8730e loadable_apps build
```

전체 툴체인 산출물을 수집하려면 같은 빌드에 `TIZENRT_FULL_ARTIFACTS=1`을
추가한다. 이 모드에서는 일반 `.o`, `.a`, `.bin`, `.trpk` 외에 전처리 결과,
assembly, listing, dependency, archive content, link 분석 파일까지 별도 tree로
복사한다.

```sh
TIZENRT_FULL_ARTIFACTS=1 ./dbuild.sh distclean configure rtl8730e loadable_apps build
```

검증 후 최종 작업 tree는 normal build 상태로 되돌렸기 때문에
`build/output/toolchain-artifacts/`는 남아 있지 않다. 위 artifact build 명령을
다시 실행하면 동일한 tree가 재생성된다.

## Configuration snapshot

`tools/configure.sh rtl8730e/loadable_apps` 단계에서 `build/configs/rtl8730e/loadable_apps/defconfig`와 `build/configs/rtl8730e/Make.defs`가 `os/` 아래로 복사된다.

| artifact | generated at | size | role |
| --- | ---: | ---: | --- |
| `os/.config` | 2026-07-11 00:49:32 | 39,069 | 빌드 설정 |
| `os/Make.defs` | 2026-07-11 00:49:32 | 11,295 | board/compiler/postbuild rule 설정 |
| `os/.version` | 2026-07-11 00:49:47 | 214 | context 단계에서 생성된 버전 메타데이터 |
| `os/.bininfo` | 2026-07-11 01:15:56 | 128 | 최종 패키지 이름 매핑 |
| `os/build.log` | 2026-07-11 01:15:58 | 457,992 | 빌드 전체 로그 |
| `os/include/tinyara/config.h` | 2026-07-11 00:49:46 | 27,531 | `.config` 기반 generated config header |
| `os/include/tinyara/version.h` | 2026-07-11 00:49:48 | 491 | `.version` 기반 generated version header |
| `os/include/math.h` | 2026-07-11 00:49:48 | 48,692 | include export copy |
| `os/include/stdarg.h` | 2026-07-11 00:49:48 | 3,589 | include export copy |

주요 설정값:

```text
CONFIG_DOCKER_VERSION="2.0.0"
CONFIG_ARCH_BOARD="rtl8730e"
CONFIG_APP_BINARY_SEPARATION=y
CONFIG_SUPPORT_COMMON_BINARY=y
CONFIG_COMMON_BINARY_NAME="common"
CONFIG_APP1_INFO=y
CONFIG_APP1_BIN_NAME="app1"
CONFIG_BUILD_PROTECTED=y
CONFIG_XIP_ELF=y
CONFIG_FLASH_PART_NAME="bl1,reserved,ftl,ss,kernel,common,app1,kernel,common,app1,userfs,micom,bootparam,"
CONFIG_FLASH_PART_SIZE="60,40,12,400,1844,3040,1384,1844,3040,1384,2048,1280,8,"
```

`.bininfo` 내용:

```text
KERNEL_BIN_NAME=kernel_rtl8730e_200204.trpk
APP1_BIN_NAME=app1_rtl8730e_190412.trpk
COMMON_BIN_NAME=common_rtl8730e_200204.trpk
```

## Phase relationship

1. `distclean`
   - 이전 `.config`, `.bininfo`, `build/output` 산출물을 제거한다.
   - 이번 빌드의 산출물을 새로 만들기 위한 초기화 단계이다.

2. `configure`
   - `rtl8730e/loadable_apps` 설정을 `os/.config`와 `os/Make.defs`로 복사한다.
   - 이후 모든 산출물 이름, partition size, app/common 분리 여부가 여기서 결정된다.

3. `make context`
   - `.version`, generated headers, include/arch/app symlink를 만든다.
   - 이 단계 산출물은 컴파일 입력으로 사용된다.

4. `make pass1/pass2`
   - 각 subsystem object/dependency 파일을 만든 뒤 `build/output/libraries/lib*.a`로 묶는다.
   - `pass1`은 user/common 쪽 준비물을 만들고, `pass2`는 kernel link 산출물 `tinyara.axf`를 만든다.

5. app/common packaging
   - `app1.relelf`와 common link 결과에서 debug ELF와 raw binary를 만든다.
   - header/checksum을 붙여 `app1`, `common`, `build/output/bin/user/*`가 생성된다.

6. Realtek board postbuild
   - `tinyara.axf`를 `target_img2.axf`, `target_pure_img2.axf`, `ram_2.bin`, `xip_image2.bin` 등으로 변환한다.
   - board bootloader/blob과 합쳐 `ap_image_all.bin`과 `km0_km4_ap_image_all.bin`을 만든다.

7. Samsung header and final package
   - `km0_km4_ap_image_all.bin`, `app1`, `common`에 TizenRT package header를 적용한다.
   - `.bininfo` 이름대로 `kernel_*.trpk`, `app1_*.trpk`, `common_*.trpk`가 최종 배포 패키지가 된다.
   - `bootparam.bin`, `tinyara_binarysize.txt`, header/size 검증 로그가 마지막에 생성된다.

## Full toolchain artifacts

`TIZENRT_FULL_ARTIFACTS=1`일 때만 `build/output/toolchain-artifacts/`가 생성된다.
normal build에서는 이 directory를 만들지 않고, `clean`/`distclean`에서도 제거한다.

최종 검증된 artifact build inventory:

| class | files | generated when | representative outputs |
| --- | ---: | --- | --- |
| `compile` | 9,580 | 각 C/C++ source가 object로 컴파일될 때 | `.i`, `.ii`, `.s`, `.d`, `.lst`, `.o` |
| `assemble` | 84 | 각 assembly source가 object로 assemble될 때 | `.preprocessed.s`, `.d`, `.lst`, `.o` |
| `archive` | 56 | `ar`가 subsystem archive를 만들 때 | `lib*.a`, `*.contents` |
| `link` | 6 | kernel ELF link 직후 | `System.map`, `tinyara.map`, `tinyara.nm`, `tinyara.objdump`, `tinyara.readelf`, `tinyara.size` |
| `package` | 11 | board postbuild와 final package 단계 | debug ELF, final `.trpk`, `bootparam.bin`, `target_*`, `km0_km4_ap_image_all.hex` |

`manifest.tsv`는 package copy까지 끝난 뒤 생성된다. `manifest.tsv` 자신은 목록에서
제외하며, 최종 검증 기준 manifest entry 수는 9,737개이다.

### Artifact tree relationship

```text
build/output/toolchain-artifacts/
  compile/<source-relative-dir>/
    -> C:  <object-base>.i  -> <object-base>.s -> <object-base>.o
    -> C++:<object-base>.ii -> <object-base>.s -> <object-base>.o
    -> <object-base>.d, <object-base>.lst
  assemble/<source-relative-dir>/
    -> <object-base>.preprocessed.s -> <object-base>.o
    -> <object-base>.d, <object-base>.lst
  archive/
    -> copied lib*.a
    -> *.contents from `ar t`
  link/
    -> tinyara map/symbol/disassembly/readelf/size reports
  package/
    -> app1_dbg, common_dbg
    -> kernel_rtl8730e_200204.trpk
    -> app1_rtl8730e_190412.trpk
    -> common_rtl8730e_200204.trpk
    -> bootparam.bin
    -> target_img2.axf, target_pure_img2.axf, target_img2.map, target_img2.asm
    -> km0_km4_ap_image_all.hex
  manifest.tsv
```

package class의 최종 파일 크기:

| artifact | size | relation |
| --- | ---: | --- |
| `app1_dbg` | 102,820 | app1 debug ELF |
| `app1_rtl8730e_190412.trpk` | 449 | final app1 package |
| `bootparam.bin` | 8,192 | BP1/BP2 boot parameter image |
| `common_dbg` | 1,507,276 | common debug ELF |
| `common_rtl8730e_200204.trpk` | 147,270 | final common package |
| `kernel_rtl8730e_200204.trpk` | 1,543,562 | final kernel package |
| `km0_km4_ap_image_all.hex` | 4,341,715 | Intel HEX conversion of kernel image |
| `target_img2.asm` | 10,687,124 | Realtek disassembly output |
| `target_img2.axf` | 7,576,504 | Realtek postbuild ELF |
| `target_img2.map` | 231,017 | Realtek image map |
| `target_pure_img2.axf` | 1,043,796 | stripped image ELF |

`target_img2.axf`의 byte size는 artifact build package copy에서 7,576,504로
기록됐고, 최종 normal build filesystem에서는 7,576,424로 관찰됐다. 최종
`.trpk` 크기와 partition/header 검증 결과는 동일하며, 두 빌드 로그의
`target_img2.axf` section total은 동일하다.

## Libraries

`build/output/libraries`의 `lib*.a`는 subsystem별 object 파일을 모은 archive이다. 이후 `tinyara.axf`, `common`, `app1` link 입력으로 사용된다.

| artifact | generated at | size | relation |
| --- | ---: | ---: | --- |
| `build/output/libraries/libproxies.a` | 2026-07-11 00:56:50 | 710,748 | syscall/proxy archive |
| `build/output/libraries/libuc.a` | 2026-07-11 00:58:12 | 2,671,660 | user libc archive |
| `build/output/libraries/libumm.a` | 2026-07-11 00:58:27 | 677,608 | user mm archive |
| `build/output/libraries/libuarch.a` | 2026-07-11 00:58:28 | 7,830 | user arch archive |
| `build/output/libraries/libuwque.a` | 2026-07-11 00:58:31 | 60,338 | user work queue archive |
| `build/output/libraries/libframework.a` | 2026-07-11 00:58:48 | 979,994 | framework archive |
| `build/output/libraries/libcxx.a` | 2026-07-11 01:00:06 | 7,963,642 | C++ runtime archive |
| `build/output/libraries/libexternal.a` | 2026-07-11 01:01:46 | 4,670,280 | external components archive |
| `build/output/libraries/libiotivity.a` | 2026-07-11 01:01:46 | 3,387,598 | IoTivity archive |
| `build/output/libraries/libapps.a` | 2026-07-11 01:02:07 | 480,906 | apps archive |
| `build/output/libraries/libkernel.a` | 2026-07-11 01:09:42 | 3,026,266 | kernel archive |
| `build/output/libraries/libstubs.a` | 2026-07-11 01:10:01 | 652,200 | protected build stub archive |
| `build/output/libraries/libkc.a` | 2026-07-11 01:11:12 | 2,681,020 | kernel libc archive |
| `build/output/libraries/libkmm.a` | 2026-07-11 01:11:25 | 876,670 | kernel mm archive |
| `build/output/libraries/libkarch.a` | 2026-07-11 01:11:44 | 914,392 | kernel arch archive |
| `build/output/libraries/libkwque.a` | 2026-07-11 01:11:46 | 76,428 | kernel work queue archive |
| `build/output/libraries/libnet.a` | 2026-07-11 01:12:11 | 2,048,342 | network archive |
| `build/output/libraries/libse.a` | 2026-07-11 01:12:14 | 161,440 | security engine archive |
| `build/output/libraries/libcompression.a` | 2026-07-11 01:12:20 | 197,942 | compression archive |
| `build/output/libraries/libpm.a` | 2026-07-11 01:12:24 | 261,824 | power management archive |
| `build/output/libraries/libfs.a` | 2026-07-11 01:12:50 | 1,305,974 | filesystem archive |
| `build/output/libraries/libdrivers.a` | 2026-07-11 01:13:37 | 655,570 | drivers archive |
| `build/output/libraries/libbinfmt.a` | 2026-07-11 01:13:45 | 289,598 | binary loader archive |

`build/output/libraries/.gitignore`, `Makefile`, `README.txt`는 이번 빌드 생성물이 아니라 기존 placeholder/support 파일이다.

## Binary artifacts

### app/common packaging

| artifact | generated at | size | relation |
| --- | ---: | ---: | --- |
| `build/output/bin/app1_0.ld` | 2026-07-11 01:13:45 | 165 | app1 section link script |
| `build/output/bin/app1_1.ld` | 2026-07-11 01:13:45 | 165 | app1 section link script |
| `build/output/bin/common_0.ld` | 2026-07-11 01:13:45 | 164 | common section link script |
| `build/output/bin/common_1.ld` | 2026-07-11 01:13:45 | 164 | common section link script |
| `build/output/bin/app1.relelf` | 2026-07-11 01:13:48 | 10,404 | relocatable app1 ELF |
| `build/output/bin/lib_symbols.txt` | 2026-07-11 01:13:48 | 555 | app/common symbol metadata |
| `build/output/bin/common.map` | 2026-07-11 01:13:49 | 724,724 | common link map |
| `build/output/bin/app1_dbg` | 2026-07-11 01:15:30 | 102,820 | app1 debug ELF |
| `build/output/bin/app1.bin` | 2026-07-11 01:15:30 | 401 | app1 raw binary |
| `build/output/bin/app1_without_header` | 2026-07-11 01:15:31 | 401 | app1 raw copy before package header |
| `build/output/bin/app1` | 2026-07-11 01:15:31 | 449 | app1 package with header |
| `build/output/bin/user/app1` | 2026-07-11 01:15:31 | 449 | user-visible copy of app1 package |
| `build/output/bin/common_dbg` | 2026-07-11 01:15:31 | 1,507,276 | common debug ELF |
| `build/output/bin/common.bin` | 2026-07-11 01:15:31 | 147,254 | common raw binary |
| `build/output/bin/common_without_header` | 2026-07-11 01:15:32 | 147,254 | common raw copy before package header |
| `build/output/bin/common` | 2026-07-11 01:15:32 | 147,270 | common package with header |
| `build/output/bin/user/common` | 2026-07-11 01:15:32 | 147,270 | user-visible copy of common package |

### kernel link and Realtek postbuild

| artifact | generated at | size | relation |
| --- | ---: | ---: | --- |
| `build/output/bin/tinyara.axf` | 2026-07-11 01:15:26 | 7,576,424 | primary kernel ELF link output |
| `build/output/bin/tinyara.map` | 2026-07-11 01:15:26 | 3,278,779 | kernel link map |
| `build/output/bin/System.map` | 2026-07-11 01:15:30 | 451,383 | kernel symbol map |
| `build/output/bin/target_img2.axf` | 2026-07-11 01:15:32 | 7,576,424 | Realtek postbuild copy of `tinyara.axf` |
| `build/output/bin/target_img2.map` | 2026-07-11 01:15:32 | 231,017 | Realtek image map |
| `build/output/bin/target_img2.asm` | 2026-07-11 01:15:35 | 10,687,124 | Realtek disassembly output |
| `build/output/bin/target_pure_img2.axf` | 2026-07-11 01:15:36 | 1,043,796 | stripped image ELF |
| `build/output/bin/APP.trace` | 2026-07-11 01:15:36 | 59,879 | Bluetooth trace section dump |
| `build/output/bin/ram_2.bin` | 2026-07-11 01:15:36 | 15,588 | RAM image section |
| `build/output/bin/xip_image2.bin` | 2026-07-11 01:15:36 | 796,160 | XIP image section |
| `build/output/bin/ca32_image2_all.bin` | 2026-07-11 01:15:36 | 15,588 | CA32 image bundle |
| `build/output/bin/bl1.bin` | 2026-07-11 01:15:36 | 14,057 | copied board bootloader blob |
| `build/output/bin/bl1_sram.bin` | 2026-07-11 01:15:36 | 32 | copied SRAM bootloader blob |
| `build/output/bin/fip.bin` | 2026-07-11 01:15:36 | 286,721 | generated FIP image |
| `build/output/bin/bl1_prepend.bin` | 2026-07-11 01:15:37 | 14,089 | header-prepended `bl1.bin` |
| `build/output/bin/bl1_sram_prepend.bin` | 2026-07-11 01:15:37 | 64 | header-prepended `bl1_sram.bin` |
| `build/output/bin/xip_image2_prepend.bin` | 2026-07-11 01:15:37 | 796,192 | header-prepended XIP image |
| `build/output/bin/fip_prepend.bin` | 2026-07-11 01:15:38 | 286,753 | header-prepended FIP image |
| `build/output/bin/flash_loader_ram_1.bin` | 2026-07-11 01:15:38 | 1,888 | copied flash loader blob |
| `build/output/bin/km4_boot_all.bin` | 2026-07-11 01:15:38 | 54,176 | copied KM4 boot blob |
| `build/output/bin/km0_km4_app.bin` | 2026-07-11 01:15:38 | 438,560 | copied KM0/KM4 app blob |
| `build/output/bin/manifest.bin` | 2026-07-11 01:15:38 | 4,096 | manifest inserted into image bundle |
| `build/output/bin/target_FPGA.axf` | 2026-07-11 01:15:38 | 38,520 | copied flash-loader AXF |
| `build/output/bin/ap_image_all.bin` | 2026-07-11 01:15:38 | 1,101,194 | Realtek AP image bundle |

### final deployable packages and validation artifacts

| artifact | generated at | size | relation |
| --- | ---: | ---: | --- |
| `build/output/bin/km0_km4_ap_image_all_without_header.bin` | 2026-07-11 01:15:56 | 1,543,546 | kernel image before package header |
| `build/output/bin/km0_km4_ap_image_all.bin` | 2026-07-11 01:15:56 | 1,543,562 | kernel image with package header |
| `build/output/bin/kernel_rtl8730e_200204.trpk` | 2026-07-11 01:15:56 | 1,543,562 | final kernel package copied from `km0_km4_ap_image_all.bin` |
| `build/output/bin/app1_rtl8730e_190412.trpk` | 2026-07-11 01:15:56 | 449 | final app1 package copied from `app1` |
| `build/output/bin/common_rtl8730e_200204.trpk` | 2026-07-11 01:15:56 | 147,270 | final common package copied from `common` |
| `build/output/bin/km0_km4_ap_image_all.hex` | 2026-07-11 01:15:56 | 4,341,715 | Intel HEX conversion of kernel image |
| `build/output/bin/bootparam.bin` | 2026-07-11 01:15:57 | 8,192 | BP1/BP2 boot parameter image |
| `build/output/bin/tinyara_binarysize.txt` | 2026-07-11 01:15:57 | 74 | latest size-check report line |

`build/output/bin/.gitignore`는 이번 빌드 생성물이 아니라 기존 placeholder 파일이다.

## Verification result

normal build와 full toolchain artifact build를 모두 검증했다.

| command | result | artifact tree |
| --- | --- | --- |
| `./dbuild.sh distclean configure rtl8730e loadable_apps build` | PASS | 생성하지 않음 |
| `TIZENRT_FULL_ARTIFACTS=1 ./dbuild.sh distclean configure rtl8730e loadable_apps build` | PASS | `build/output/toolchain-artifacts/` 생성 |

두 빌드 모두 다음 최종 검증을 통과했다.

```text
Partition verification SUCCESS!! The setting of all partitions is OK.
KERNEL | 1,543,562 bytes | 1,888,256 bytes | 81.75% | PASS
APP1   | 449 bytes       | 1,417,216 bytes | 0.03%  | PASS
COMMON | 147,270 bytes   | 3,112,960 bytes | 4.73%  | PASS
Size verification SUCCESS!! The size of all binaries are OK.
Header verification SUCCESS!!  # common
Header verification SUCCESS!!  # kernel
Header verification SUCCESS!!  # app1
```

`tinyara_binarysize.txt`는 마지막으로 기록된 common 검증 줄만 보관한다.

```text
COMMON | 147,270 bytes | 3,112,960 bytes | 4.73% | :heavy_check_mark:PASS
```

## Short dependency graph

```text
rtl8730e/loadable_apps defconfig + rtl8730e Make.defs
  -> os/.config, os/Make.defs
  -> generated headers and symlinks
  -> subsystem objects
     -> full artifact mode: compile/ and assemble/ preprocessed, assembly, dependency, listing, object copies
  -> build/output/libraries/lib*.a
     -> full artifact mode: archive/ copied archives and archive contents
  -> app/common link materials
     -> app1.relelf -> app1_dbg -> app1.bin -> app1_without_header -> app1 -> app1_*.trpk
     -> common_dbg  -> common.bin -> common_without_header -> common -> common_*.trpk
  -> kernel link
     -> tinyara.axf, tinyara.map, System.map
     -> full artifact mode: link/ map, nm, objdump, readelf, size reports
     -> target_img2.axf, target_pure_img2.axf, ram_2.bin, xip_image2.bin
     -> bl1/fip/bootloader/blob inputs
     -> ap_image_all.bin
     -> km0_km4_ap_image_all_without_header.bin
     -> km0_km4_ap_image_all.bin
     -> kernel_*.trpk, km0_km4_ap_image_all.hex
     -> full artifact mode: package/ copied debug ELF, final packages, bootparam and Realtek postbuild files
  -> bootparam.bin
  -> partition, size, header verification
```
