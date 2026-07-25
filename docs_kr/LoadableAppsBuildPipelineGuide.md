# Loadable Apps 빌드 파이프라인 가이드

이 문서는 `loadable_apps`가 TizenRT 전체 빌드 안에서 어떻게 컴파일, 링크, 패키징되는지 사람이 수정 가능한 수준으로 설명한다. 예시는 `rtl8730e/loadable_apps`를 기준으로 한다.

## 기준 설정

`rtl8730e/loadable_apps`는 loadable app을 kernel과 분리해서 만드는 protected build 설정이다.

```text
CONFIG_BUILD_PROTECTED=y
CONFIG_APP_BINARY_SEPARATION=y
CONFIG_NUM_APPS=1
CONFIG_SUPPORT_COMMON_BINARY=y
CONFIG_COMMON_BINARY_NAME="common"
# CONFIG_ELF is not set
CONFIG_XIP_ELF=y
CONFIG_COMPRESSED_BINARY=y
CONFIG_FLASH_PARTITION=y
CONFIG_APP1_INFO=y
CONFIG_APP1_BIN_NAME="app1"
CONFIG_APP1_BIN_TYPE="ELF"
CONFIG_APP1_BIN_VER=190412
CONFIG_APP1_BIN_DYN_RAMSIZE=5242880
CONFIG_APP1_MAIN_STACKSIZE=8192
CONFIG_APP1_MAIN_PRIORITY=180
# CONFIG_APP2_INFO is not set
```

이 조합에서는 `app1` 하나와 `common` binary가 만들어진다. `CONFIG_NUM_APPS`가 2보다 작기 때문에 sample의 `micomapp`은 최종 build 대상이 아니다.

## 전체 흐름

```text
os/dbuild.sh
  -> docker run ... make
    -> os/Makefile.unix
      -> context
      -> pass1deps
      -> pass2deps
      -> pass1
        -> loadable_apps/Makefile undefsym
        -> common binary link
        -> loadable_apps/Makefile all
      -> pass2
        -> kernel binary link
      -> post
        -> app/common verify
        -> strip, objcopy, compression, binary header, checksum
        -> board-specific binary packaging
        -> .bininfo, convert_binary.py, validate_output.py
```

## 1. configure 단계

`./dbuild.sh distclean configure rtl8730e loadable_apps build`를 실행하면 `configure` 단계에서 다음 파일이 설치된다.

| 원본 | 설치 위치 | 역할 |
| --- | --- | --- |
| `build/configs/rtl8730e/loadable_apps/defconfig` | `os/.config` | Kconfig 값과 앱 binary 설정 |
| `build/configs/rtl8730e/Make.defs` | `os/Make.defs` | 툴체인, 플래그, 보드 후크 |

`os/.config`는 빌드 중 `include/tinyara/config.h`를 만드는 입력이고, `os/Make.defs`는 `CC`, `LD`, `AR`, `OBJCOPY`, `CELFFLAGS`, `LDELFFLAGS`, `MAKE_BOARD_SPECIFIC_BIN` 같은 make 변수를 제공한다.

`rtl8730e/Make.defs`에서 loadable app에 중요한 값은 다음 계열이다.

```make
ifeq ($(CONFIG_APP_BINARY_SEPARATION),y)
CELFFLAGS = $(CFLAGS) -mlong-calls
CXXELFFLAGS = $(CXXFLAGS) -mlong-calls
LDELFFLAGS = -r -e main
LDELFFLAGS += -T $(TOPDIR)/userspace/userspace_apps.ld
endif
```

XIP ELF에서는 앱별 link script가 나중에 `build/output/bin/<app>_0.ld`로 만들어지고, `loadable.mk`가 그것을 사용한다.

## 2. os/Makefile.unix가 빌드 그래프를 만든다

`os/Makefile.unix`는 다음 순서로 빌드 설정을 읽는다.

```make
-include $(TOPDIR)/.config
include $(TOPDIR)/tools/Config.mk
-include $(TOPDIR)/Make.defs
include Directories.mk
include ProtectedLibs.mk
```

`Directories.mk`는 build mode에 따라 디렉터리를 나눈다. protected build에서는 일반 앱, framework, external, tools가 사용자 영역 add-on으로 들어간다.

```make
ifeq ($(CONFIG_BUILD_PROTECTED),y)
USER_ADDONS += $(APPDIR)
USER_ADDONS += $(EXTDIR)
USER_ADDONS += $(FRAMEWORK_LIB_DIR)
USER_ADDONS += $(TOOLSDIR)
endif

USERDIRS += $(LIB_DIR)/libc mm wqueue$(USER_ADDONS)
USERDIRS += $(TOPDIR)/../loadable_apps
```

즉 `loadable_apps`는 일반 `apps/`와 별개로 `USERDIRS`에 들어간다. 이 때문에 clean, distclean, depend 대상에도 포함되고, pass1에서 별도 binary를 만들 수 있다.

`ProtectedLibs.mk`는 최종 링크 입력을 kernel과 user로 나눈다.

```make
TINYARALIBS += libkernel, libstubs, libkc, libkmm, libkarch, ...
USERLIBS += libproxies, libuc, libumm, libuarch, libuwque, libframework
USERLIBS += libexternal
USERLIBS += libapps
```

loadable app은 자기 소스만 링크하는 것이 아니라, 이 `USERLIBS`와 userspace startup object를 함께 사용한다.

## 3. context와 dependency 단계

`$(BIN): pass1deps pass2deps pass1 pass2 post`가 최종 target이다. 먼저 `context`가 실행된다.

`context`의 핵심 결과는 다음이다.

- `os/.version` 생성
- `include/arch`, `include/apps`, `arch/arm/src/board` 같은 링크 생성
- `include/tinyara/config.h` 같은 생성 header 준비
- `apps` builtin registry 갱신
- compression tool 생성

그 다음 `pass1dep`가 `USERDEPDIRS`를 순회한다.

```make
pass1dep: context tools/mkdeps tools/compression/mkcompresstool.sh
	for dir in $(USERDEPDIRS); do make -C $$dir depend; done
```

이 단계에서 user-space syscall proxy, libc, apps, external, framework, `loadable_apps`의 dependency가 준비된다. 실제 빌드 로그에서는 `os/syscall/proxies/PROXY_*.c`가 생성되고 `libproxies.a`로 archive되는 것을 볼 수 있다.

## 4. loadable_apps 대상 선정

`loadable_apps/Makefile`은 바로 앱을 하드코딩하지 않는다.

```make
BUILDIRS := $(dir $(wildcard */Make.defs))
CONFIGURED =
$(foreach BDIR, $(BUILDIRS), $(eval include $(BDIR)Make.defs))
```

현재는 `loadable_sample/Make.defs`가 먼저 포함되고, 그 안에서 `CONFIG_EXAMPLES_LOADABLE`이 켜져 있으면 sample 앱들의 `Make.defs`를 포함한다.

```make
ifneq ($(CONFIG_EXAMPLES_LOADABLE),)
include $(wildcard loadable_sample/*/Make.defs)
endif
```

sample 앱 등록은 다음과 같다.

```make
# wifiapp/Make.defs
CONFIGURED += loadable_sample/wifiapp

# micomapp/Make.defs
ifeq ($(shell expr $(CONFIG_NUM_APPS) '>' 1), 1)
CONFIGURED += loadable_sample/micomapp
endif
```

`rtl8730e/loadable_apps`는 `CONFIG_NUM_APPS=1`이므로 `CONFIGURED`에는 `loadable_sample/wifiapp`만 들어간다.

## 5. 앱 Makefile과 loadable.mk

`wifiapp/Makefile`은 app binary separation이 켜져 있을 때만 `loadable.mk`로 들어간다.

```make
ifeq ($(CONFIG_APP_BINARY_SEPARATION),y)
BIN = $(CONFIG_APP1_BIN_NAME)
include $(TOPDIR)/$(LOADABLEDIR)/loadable.mk
endif
```

`loadable.mk`의 역할은 세 가지다.

1. 앱 소스 `.c`를 `__APP_BUILD__` define과 `CELFFLAGS`로 컴파일한다.
2. `os/userspace`에서 `up_userspace.o` 또는 common userspace object를 만든다.
3. 앱 object와 userspace object를 링크해 앱 ELF를 만든 뒤 `build/output/bin`에 설치한다.

핵심 변수는 다음이다.

```make
APP_OBJS = $(SRCS:.c=$(OBJEXT))
USERSPACE_OBJ = $(TOPDIR)/userspace/up_userspace$(OBJEXT)
OBJS = $(APP_OBJS) $(USERSPACE_OBJ)
APPDEFINE = __APP_BUILD__
```

일반 ELF 모드에서는 `LD`가 `LDELFFLAGS`, `USERLIBS`, `LIBGCC`, `LIBSUPXX`를 묶어 앱 binary를 만든다. `rtl8730e/loadable_apps`처럼 `CONFIG_XIP_ELF=y`이면 별도 경로를 탄다.

```make
$(LD) \
  -T $(USER_BIN_DIR)/$(BIN)_0.ld \
  -T $(TOPDIR)/../build/configs/$(CONFIG_ARCH_BOARD)/scripts/xipelf/userspace_all.ld \
  -e main \
  -o $(BIN) \
  $(ARCHCRT0OBJ) $(OBJS) \
  --start-group $(LIBGCC) $(LIBSUPXX) --end-group \
  -R $(USER_BIN_DIR)/$(CONFIG_COMMON_BINARY_NAME)
```

여기서 `-R common`은 앱이 common binary의 심볼을 참조하도록 만드는 부분이다. 그래서 common binary 설정에서는 앱 undefined symbol 검증 순서가 일반 ELF와 다르다.

## 6. common binary와 app binary 순서

현재 설정은 `CONFIG_SUPPORT_COMMON_BINARY=y`이고 `CONFIG_XIP_ELF=y`다. `os/Makefile.unix`의 pass1은 다음 순서로 동작한다.

1. `tools/mkldscript.py`가 앱과 common용 linker script를 생성한다.
2. `make -C userspace common_obj`가 common userspace object를 만든다.
3. `make -C loadable_apps undefsym`이 앱을 임시 링크해 필요한 undefined symbol을 `build/output/bin/lib_symbols.txt`에 모은다.
4. `LD -o build/output/bin/common ... $(cat lib_symbols.txt)`로 common binary를 링크한다.
5. `make -C loadable_apps all`이 최종 앱 binary를 다시 링크한다.

앱 하나만 보면 `loadable_apps/loadable_sample/wifiapp`에서 다음 일이 생긴다.

```text
wifiapp.c
  -> wifiapp.o
userspace/up_userspace.o
  -> app1 또는 app1.relelf
  -> loadable_sample/wifiapp/build/app1
  -> build/output/bin/app1
```

`install` target은 앱 디렉터리의 `build/<BIN>`을 `build/output/bin/<BIN>`으로 복사한다.

## 7. post 단계의 binary 후처리

`pass2`에서 kernel binary가 링크된 뒤 `post` target이 실행된다. loadable app에 직접 관련된 처리는 `PREPARE_APP`이다.

XIP ELF 기준으로 app1은 다음 순서로 처리된다.

```text
build/output/bin/app1
  -> app1_dbg 복사
  -> debug symbol strip
  -> objcopy -O binary app1 app1.bin
  -> app1.bin을 app1로 복사
  -> mkbinheader.py app1 user ELF app1 ...
  -> mkchecksum.py app1
  -> build/output/bin/user/app1 복사
```

common binary도 같은 post target에서 검증과 header/checksum 처리를 거친다.

그 뒤 board 공통 후처리가 이어진다.

```text
MAKE_BOARD_SPECIFIC_BIN
MAKE_SAMSUNG_HEADER
set_bininfo.py
convert_binary.py
validate_output.py
```

`set_bininfo.py`는 최종 패키지 이름을 `os/.bininfo`에 기록한다. `convert_binary.py`는 `app1` 같은 내부 이름을 `app1_<board>_<version>.trpk` 형태의 최종 이름으로 복사한다. `validate_output.py`는 partition 구성, binary 크기, `.trpk` header를 확인한다.

## 8. 산출물 읽는 법

빌드가 끝난 뒤 먼저 본다.

```bash
cd os
cat .bininfo
find ../build/output/bin -maxdepth 2 -type f | sort
```

중요한 파일은 다음 범주로 나뉜다.

| 범주 | 예시 | 의미 |
| --- | --- | --- |
| 내부 app 이름 | `build/output/bin/app1` | post 처리된 app binary |
| debug copy | `build/output/bin/app1_dbg` | strip 전 앱 ELF |
| common | `build/output/bin/common` | 앱들이 공유하는 common binary |
| 최종 패키지 | `app1_<board>_<version>.trpk` | `.bininfo`와 download/package 검증 기준 이름 |
| user 복사본 | `build/output/bin/user/app1` | user binary 묶음에 들어갈 복사본 |
| kernel | `kernel_<board>_<version>.trpk` 또는 board별 이름 | pass2와 board packaging 결과 |

정확한 최종 이름은 board metadata와 `set_bininfo.py` 결과에 따라 달라진다. 따라서 문서나 자동화는 하드코딩된 파일명보다 `os/.bininfo`를 우선 읽어야 한다.

실제 `rtl8730e/loadable_apps` 빌드에서는 다음 파일명이 기록됐다.

```text
KERNEL_BIN_NAME=kernel_rtl8730e_200204.trpk
APP1_BIN_NAME=app1_rtl8730e_190412.trpk
COMMON_BIN_NAME=common_rtl8730e_200204.trpk
```

`build/output/bin`에는 `app1`, `app1.bin`, `app1.relelf`, `app1_dbg`, `app1_rtl8730e_190412.trpk`, `common`, `common.bin`, `common_dbg`, `common_rtl8730e_200204.trpk`, `kernel_rtl8730e_200204.trpk`, `user/app1`, `user/common`이 생성됐다. size verification 결과는 `KERNEL`, `APP1`, `COMMON` 모두 `PASS`였고 header verification도 모두 `SUCCESS`였다.

## 9. 빌드 파이프라인을 수정할 때 볼 위치

| 수정 목적 | 먼저 볼 파일 | 이유 |
| --- | --- | --- |
| loadable app 개수 변경 | `build/configs/<board>/<config>/defconfig` | `CONFIG_NUM_APPS`, `CONFIG_APP2_INFO`가 대상 앱 수를 결정한다. |
| app1/app2 이름, version, stack, priority 변경 | `defconfig` | `CONFIG_APP*_BIN_*` 값이 header와 binary manager metadata에 들어간다. |
| sample 앱 추가/제외 | `loadable_apps/loadable_sample/*/Make.defs` | `CONFIGURED += ...`가 실제 build 대상이다. |
| 앱 소스 추가 | 앱 디렉터리 `Makefile` | `SRCS`에 들어가야 object와 link 입력이 된다. |
| loadable link 방식 변경 | `loadable_apps/loadable.mk` | app object, userspace object, LD command가 여기 있다. |
| common binary link 변경 | `os/Makefile.unix` pass1 | `lib_symbols.txt`, common link, loadable 재링크 순서가 여기 있다. |
| kernel/user 라이브러리 구성 변경 | `os/ProtectedLibs.mk`, `os/Directories.mk` | `USERLIBS`, `TINYARALIBS`, `USERDIRS`가 여기서 나온다. |
| board별 package 형식 변경 | `build/configs/<board>/Make.defs`, `*_make_bin.sh` | `MAKE_BOARD_SPECIFIC_BIN` hook과 board script가 최종 binary를 만든다. |
| partition size 실패 수정 | `defconfig`, `os/tools/check_package_size.py` | app/common/kernel partition 이름과 크기를 비교한다. |

수정 원칙은 원본을 고치는 것이다. `os/.config`, `include/tinyara/config.h`, `os/.bininfo`, `build/output/bin/*`는 빌드 산출물이므로 직접 고쳐도 다음 clean/configure/build에서 사라진다.
