# CFLAGS 확인 및 변경 가이드

이 문서는 TizenRT 빌드에서 `CFLAGS`가 어떻게 조합되는지, 현재
`rtl8730e` 설정에서 각 옵션이 무슨 의미인지, 그리고 옵션을 바꾸려면
어느 파일을 수정해야 하는지 정리한다.

아래 내용은 현재 설정된 트리를 기준으로 한다.

- 보드: `rtl8730e`
- 칩: `amebasmart`
- 아키텍처 패밀리: `armv7-a`
- 툴체인 prefix: `arm-none-eabi-`
- 빌드 모드: protected build
- Docker 빌드 루트: `/root/tizenrt`

## CFLAGS가 쓰이는 위치

공통 C 컴파일 매크로는 `os/tools/Config.mk`에 있다.

```make
$(CC) -c $(CFLAGS) $1 -o $2
```

현재 Docker 빌드에서는 이것이 `arm-none-eabi-gcc -c $(CFLAGS)` 형태로
실행된다.

## 현재 CFLAGS 확인 방법

저장소 루트에서 아래 명령을 실행한다. `os/dbuild.sh`와 같은 Docker
마운트 경로(`/root/tizenrt`)를 사용한다.

```bash
docker run --rm -i \
  -v "$PWD":/root/tizenrt \
  -w /root/tizenrt/os \
  tizenrt/tizenrt:1.5.8 \
  bash -lc 'printf "%s\n" \
    "TOPDIR := /root/tizenrt/os" \
    "include \$(TOPDIR)/Make.defs" \
    "\$(info CFLAGS=\$(CFLAGS))" \
    "print: ; @:" | make -f - print'
```

실제 빌드 중 컴파일 커맨드를 보고 싶다면 verbose 빌드를 사용한다.

```bash
docker run --rm -i \
  -v "$PWD":/root/tizenrt \
  -w /root/tizenrt/os \
  tizenrt/tizenrt:1.5.8 \
  make V=1
```

이미 빌드가 끝난 상태라 아무 파일도 다시 컴파일되지 않으면 `make clean`
이후 다시 빌드하거나, 확인하려는 소스 파일을 갱신한 뒤 실행한다.

## 현재 전역 CFLAGS

현재 `os/.config` 기준 전역 `CFLAGS`는 Docker 안에서 아래처럼 확장된다.

```text
-fno-common -Wall -Wstrict-prototypes -Wshadow -Wundef -fno-builtin -fno-common
-Wall -Wstrict-prototypes -Wshadow -Wundef -Wno-implicit-function-declaration
-Wno-unused-function -Wno-unused-but-set-variable
-g -Os -fno-strict-aliasing -fno-strength-reduce -fomit-frame-pointer
-mthumb -mcpu=cortex-a32 -mfloat-abi=hard -mfpu=neon-vfpv4
-I. -isystem /root/tizenrt/os/include
-isystem /root/tizenrt/os/../framework/include
-isystem /root/tizenrt/os/../external/include
-isystem /root/tizenrt/os/net/lwip/src/include
-pipe -ffunction-sections -fdata-sections
-DCONFIG_PLATFORM_AMEBAD2 -DCONFIG_USE_MBEDTLS_ROM_ALG -DDM_ODM_SUPPORT_TYPE=32
-DARM_ARCH_MAJOR=7 -DLOG_LEVEL=40 -DARMV7_SUPPORTS_LARGE_PAGE_ADDRESSING=1
-DPLAT_RO_XLAT_TABLES=0 -DPLAT_XLAT_TABLES_DYNAMIC=0 -DENABLE_BTI=0
-DHW_ASSISTED_COHERENCY=0 -DENABLE_ASSERTIONS=1
-DWARMBOOT_ENABLE_DCACHE_EARLY=0 -DSTD_PRINTF -DCONFIG_PLATFORM_TIZENRT_OS=1
```

이 전역 값은 주로 아래 파일에서 만들어진다.

- `build/configs/rtl8730e/Make.defs`: `os/tools/configure.sh`가
  `os/Make.defs`로 복사하는 보드별 빌드 정의
- `os/arch/arm/src/armv7-a/Toolchain.defs`: ARMv7-A 툴체인/CPU/FPU 옵션
- `os/.config`: 현재 선택된 Kconfig 값

## 옵션 의미와 변경 위치

| 옵션 | 의미 | 현재 출처 | 변경 위치 |
| --- | --- | --- | --- |
| `-fno-common` | tentative global definition을 common 심볼로 두지 않고 실제 정의로 취급한다. 중복 전역 변수 정의를 링크 시점에 잡는 데 도움이 된다. | `armv7-a/Toolchain.defs`, `rtl8730e/Make.defs` | 보통 유지한다. 보드 전체 정책이면 `build/configs/rtl8730e/Make.defs`를 수정한다. |
| `-Wall` | GCC의 일반적인 경고 묶음을 켠다. | `armv7-a/Toolchain.defs`, `rtl8730e/Make.defs` | 보드 전체 warning 정책이면 `build/configs/rtl8730e/Make.defs`를 수정한다. |
| `-Wstrict-prototypes` | C 함수 선언에 명시적 prototype이 없으면 경고한다. | `armv7-a/Toolchain.defs`, `rtl8730e/Make.defs` | warning 정책 변경과 동일하다. |
| `-Wshadow` | 지역 이름이 다른 선언을 가릴 때 경고한다. | `armv7-a/Toolchain.defs`, `rtl8730e/Make.defs` | warning 정책 변경과 동일하다. |
| `-Wundef` | 정의되지 않은 매크로가 `#if`에서 사용되면 경고한다. | `armv7-a/Toolchain.defs`, `rtl8730e/Make.defs` | warning 정책 변경과 동일하다. |
| `-Wno-implicit-function-declaration` | 함수 선언이 보이지 않는 상태에서 호출되어도 경고하지 않는다. | `ARCHWARNINGS` in `build/configs/rtl8730e/Make.defs` | 가능하면 이 옵션을 바꾸기보다 누락된 헤더를 먼저 고친다. |
| `-Wno-unused-function` | 사용되지 않는 static 함수 경고를 끈다. | `ARCHWARNINGS` in `build/configs/rtl8730e/Make.defs` | 보드 전체 경고를 더 엄격하게 만들 때만 수정한다. |
| `-Wno-unused-but-set-variable` | 값은 대입됐지만 읽히지 않는 변수 경고를 끈다. | `ARCHWARNINGS` in `build/configs/rtl8730e/Make.defs` | 보드 전체 경고를 더 엄격하게 만들 때만 수정한다. |
| `-fno-builtin` | 컴파일러가 표준 함수 호출을 builtin으로 치환하지 못하게 한다. | `build/configs/rtl8730e/Make.defs` | 보드 전체 codegen 정책이므로 `build/configs/rtl8730e/Make.defs`를 수정한다. |
| `-g` | 디버그 심볼을 생성한다. | `CONFIG_DEBUG_SYMBOLS=y` | `make menuconfig` 또는 선택한 `build/configs/rtl8730e/<config>/defconfig`에서 바꾼다. |
| `-Os` | 코드 크기 최적화를 수행한다. | `CONFIG_DEBUG_FULLOPT=y`, `MAXOPTIMIZATION` | `make menuconfig` 또는 선택한 defconfig에서 바꾼다. custom level은 debug optimization Kconfig 옵션을 사용한다. |
| `-fno-strict-aliasing` | strict aliasing 최적화를 끈다. 서로 다른 타입 포인터 캐스팅이 많은 코드에서 더 보수적으로 동작한다. | `armv7-a/Toolchain.defs`, `rtl8730e/Make.defs` | aliasing 안전성을 전체적으로 확인하지 않는 한 유지한다. |
| `-fno-strength-reduce` | strength-reduction 최적화를 끈다. 현재 보드 최적화 묶음에 포함되어 있다. | `build/configs/rtl8730e/Make.defs` | 보드 전체 최적화 정책이면 `build/configs/rtl8730e/Make.defs`를 수정한다. |
| `-fomit-frame-pointer` | frame pointer 레지스터를 일반 레지스터처럼 사용할 수 있게 한다. | `armv7-a/Toolchain.defs`, `rtl8730e/Make.defs` | `CONFIG_FRAME_POINTER`와 연관된다. 변경 전 `Toolchain.defs`와 보드 `Make.defs` 양쪽을 확인한다. |
| `-mthumb` | Thumb instruction set으로 코드를 생성한다. | `CONFIG_ARM_THUMB=y` | 아키텍처 Kconfig 또는 선택한 defconfig에서 바꾼다. |
| `-mcpu=cortex-a32` | Cortex-A32용 코드를 생성한다. | `CONFIG_ARCH_CORTEXA32=y` | 보드/아키텍처 Kconfig 또는 선택한 defconfig에서 바꾼다. |
| `-mfloat-abi=hard` | hard-float ABI를 사용한다. 부동소수점 인자를 FPU 레지스터로 전달한다. | `CONFIG_ARCH_FPU=y` 및 soft ABI 미선택 | FPU ABI Kconfig 또는 선택한 defconfig에서 바꾼다. 라이브러리 ABI와 맞아야 하므로 임의 변경하면 안 된다. |
| `-mfpu=neon-vfpv4` | NEON과 VFPv4 FPU 명령 사용을 허용한다. | `CONFIG_ARM_NEON=y`, `CONFIG_ARCH_CORTEXA32=y` | FPU/NEON Kconfig 또는 선택한 defconfig에서 바꾼다. |
| `-I...` | 일반 include 검색 경로를 추가한다. | 공통, 칩, 보드, 모듈 Makefile | 특정 모듈 전용 include는 해당 모듈 `Makefile` 또는 `Make.defs`에 추가한다. 좁은 범위의 include를 전역 CFLAGS에 넣지 않는다. |
| `-isystem ...` | system include 검색 경로를 추가한다. GCC가 해당 헤더의 경고를 일반 include와 다르게 취급한다. | `ARCHINCLUDES` in `build/configs/rtl8730e/Make.defs` | framework/external 전역 include 정책이면 보드 `Make.defs`에서 바꾼다. |
| `-pipe` | 컴파일 단계 사이에 임시 파일 대신 pipe를 사용한다. | `build/configs/rtl8730e/Make.defs` | 보통 유지한다. |
| `-ffunction-sections` | 함수마다 별도 ELF section에 배치한다. linker garbage collection과 함께 쓰기 좋다. | `build/configs/rtl8730e/Make.defs` | 보드 전체 codegen 정책이면 보드 `Make.defs`에서 바꾼다. |
| `-fdata-sections` | 데이터 객체마다 별도 ELF section에 배치한다. | `build/configs/rtl8730e/Make.defs` | 보드 전체 codegen 정책이면 보드 `Make.defs`에서 바꾼다. |

## 현재 preprocessor define

| define | 의미 | 변경 위치 |
| --- | --- | --- |
| `CONFIG_PLATFORM_AMEBAD2` | Realtek AmebaD2 플랫폼 코드 경로를 선택한다. | `build/configs/rtl8730e/Make.defs` |
| `CONFIG_USE_MBEDTLS_ROM_ALG` | 지원되는 mbedTLS 알고리즘에 대해 플랫폼 ROM 구현을 사용한다. | `build/configs/rtl8730e/Make.defs` |
| `DM_ODM_SUPPORT_TYPE=32` | Realtek driver/ODM 지원 타입을 지정한다. | `build/configs/rtl8730e/Make.defs` |
| `ARM_ARCH_MAJOR=7` | 플랫폼 코드에 ARM architecture major version을 알려준다. | `ADD_DEFINE` in `build/configs/rtl8730e/Make.defs` |
| `LOG_LEVEL=40` | 플랫폼 로그 레벨 define이다. | `ADD_DEFINE` in `build/configs/rtl8730e/Make.defs` |
| `ARMV7_SUPPORTS_LARGE_PAGE_ADDRESSING=1` | ARMv7 large page addressing 가정을 켠다. | `ADD_DEFINE` in `build/configs/rtl8730e/Make.defs` |
| `PLAT_RO_XLAT_TABLES=0` | translation table을 read-only platform table로 쓰지 않는다. | `ADD_DEFINE` in `build/configs/rtl8730e/Make.defs` |
| `PLAT_XLAT_TABLES_DYNAMIC=0` | dynamic translation table 지원을 끈다. | `ADD_DEFINE` in `build/configs/rtl8730e/Make.defs` |
| `ENABLE_BTI=0` | Branch Target Identification 지원을 끈다. | `ADD_DEFINE` in `build/configs/rtl8730e/Make.defs` |
| `HW_ASSISTED_COHERENCY=0` | hardware-assisted coherency를 사용하지 않는다. | `ADD_DEFINE` in `build/configs/rtl8730e/Make.defs` |
| `ENABLE_ASSERTIONS=1` | 이 define을 참조하는 플랫폼 컴포넌트의 assertion 코드를 켠다. | `ADD_DEFINE` in `build/configs/rtl8730e/Make.defs` |
| `WARMBOOT_ENABLE_DCACHE_EARLY=0` | warm boot 중 D-cache early enable을 하지 않는다. | `ADD_DEFINE` in `build/configs/rtl8730e/Make.defs` |
| `STD_PRINTF` | 플랫폼 코드에서 standard printf 경로를 선택한다. | `build/configs/rtl8730e/Make.defs` |
| `CONFIG_PLATFORM_TIZENRT_OS=1` | TizenRT OS 연동 코드 경로를 선택한다. | `build/configs/rtl8730e/Make.defs` |

일회성 실험이라면 보드 파일을 수정하지 말고 `EXTRADEFINES`를 사용한다.

```bash
cd os
make EXTRADEFINES=-DMY_TEMP_DEFINE
```

## 소스 위치별 추가 CFLAGS

전역 `CFLAGS`가 모든 소스의 최종 플래그 전체는 아니다. 각 디렉터리의
`Makefile` 또는 `Make.defs`가 include path를 추가한 뒤 `COMPILE`을 호출한다.

### `os/arch/arm/src`

아키텍처 오브젝트는 전역 `CFLAGS`에 더해
`os/arch/arm/src/amebasmart/Make.defs`와 `os/arch/arm/src/Makefile`에서
칩/아키텍처 include path를 추가한다.

대표 추가 경로:

- `/root/tizenrt/os/board/rtl8730e/include`
- `/root/tizenrt/os/board/rtl8730e/src/component/soc/amebad2/fwlib/include`
- `/root/tizenrt/os/board/rtl8730e/src/component/soc/amebad2/cmsis`
- `/root/tizenrt/os/board/rtl8730e/src/component/wifi/driver/include`
- `/root/tizenrt/os/board/rtl8730e/src/component/mbed/targets/hal/rtl8730e`
- `/root/tizenrt/os/arch/arm/src/chip`
- `/root/tizenrt/os/arch/arm/src/common`
- `/root/tizenrt/os/arch/arm/src/armv7-a`
- `/root/tizenrt/os/kernel`

칩 공통 include path는 `os/arch/arm/src/amebasmart/Make.defs`에서 바꾼다.
일반 ARM 아키텍처 include path는 `os/arch/arm/src/Makefile`에서 바꾼다.

### `os/board/rtl8730e/src`

보드 오브젝트는 전역 `CFLAGS`에 더해 board, component, Wi-Fi, mbed,
filesystem, Bluetooth, architecture include path를
`os/board/rtl8730e/src/Makefile`에서 추가한다.

현재 설정은 `CONFIG_AMEBASMART_BLE=y`라서 Bluetooth 경로도 추가된다.

- `/root/tizenrt/os/board/rtl8730e/src/component/bluetooth/inc`
- `/root/tizenrt/os/board/rtl8730e/src/component/bluetooth/profile/client`
- `/root/tizenrt/os/board/rtl8730e/src/component/bluetooth/driver/inc`
- `/root/tizenrt/os/board/rtl8730e/src/component/bluetooth/driver/inc/hci`
- `/root/tizenrt/os/board/rtl8730e/src/component/bluetooth/api/rtk_stack`
- `/root/tizenrt/os/board/rtl8730e/src/component/bluetooth/rtk_stack/inc`
- `/root/tizenrt/os/board/rtl8730e/src/component/bluetooth/rtk_stack/platform/amebad2/inc`

보드 소스 전용 include path는 `os/board/rtl8730e/src/Makefile`에서 바꾼다.
특정 subcomponent에만 필요한 include라면 board-wide Makefile보다 해당
subcomponent의 `Make.defs`를 우선한다.

## 지속 변경 원칙

옵션을 바꿀 때는 가장 좁은 책임 범위를 가진 위치를 수정한다.

| 변경 목적 | 우선 수정 위치 |
| --- | --- |
| 임시 macro 추가 | `make EXTRADEFINES=-D...` |
| CPU, FPU, debug, optimization 같은 Kconfig 옵션 | `make menuconfig`, 필요하면 선택한 `build/configs/rtl8730e/<config>/defconfig`에 반영 |
| 보드 전역 CFLAGS 또는 플랫폼 define | `build/configs/rtl8730e/Make.defs` |
| 현재 configure된 복사본만 임시 수정 | `os/Make.defs`, 단 `os/tools/configure.sh` 실행 시 덮어써질 수 있음 |
| ARMv7-A CPU flag 선택 로직 | `os/arch/arm/src/armv7-a/Toolchain.defs` |
| Amebasmart 칩 공통 include path | `os/arch/arm/src/amebasmart/Make.defs` |
| RTL8730E 보드 소스 include path | `os/board/rtl8730e/src/Makefile` |
| 특정 driver/component include path | 해당 driver/component의 `Make.defs` 또는 `Makefile` |

영구적인 보드 기본값을 바꾸려는 경우 `os/Make.defs`만 수정하면 안 된다.
`os/tools/configure.sh`는 `build/configs/rtl8730e/Make.defs`를
`os/Make.defs`로 복사하고, 선택한 `defconfig`를 `os/.config`로 복사한다.

## CXXFLAGS는 별도이다

C++ 파일은 `CFLAGS`가 아니라 `CXXFLAGS`를 사용한다. 현재 설정은
`CONFIG_HAVE_CXX=y`, `CONFIG_CXX_VERSION_11=y`라서 C++ 빌드에 다음과 같은
옵션이 추가된다.

- `-std=c++11`
- `-fcheck-new`
- `-frtti`
- `-D_LIBCPP_BUILD_STATIC`
- `-D__GLIBCXX__`
- `-fexceptions`

C++ 언어 버전과 exception 동작은 Kconfig 또는 선택한 defconfig에서 바꾼다.
보드 전역 C++ 기본값은 `build/configs/rtl8730e/Make.defs`에서 바꾼다.
