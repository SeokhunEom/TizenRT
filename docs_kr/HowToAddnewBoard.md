# 새 보드 추가(포트) 방법

새 보드를 지원하려면 *arch*, *board* 및 *configs*라는 세 가지 폴더를 변경해야 합니다.  

> 1. **arch** 폴더에는 *CPU* 및 *chip* 아키텍처가 포함되어 있습니다.
> 2. **board** 폴더에는 보드 지원 패키지(BSP)가 포함되어 있습니다.
> 3. **configs** 폴더에는 빌드 옵션과 플래시 작업이 포함되어 있습니다.

## 목차

- [폴더 구조](#folder-structure)
- [건축학](#architecture)
- [판자](#board)
- [헤더를 포함하는 방법](#how-to-include-arch-and-board-header-files)
- [구성](#configs)

<a id="folder-structure"></a>
## 폴더 구조

```
	.
	|- os .- arch - <cpu-familyname> .- include .- <cpu-archname>
	|     |                    |          |
	|     |                    |          `- <chip-name>
	|     |                    |
	|     |                    `- src .- <cpu-archname>
	|     |                           |
	|     |                           `- <chip-name>
	|     `- board - <board-name> .- include
	|                             |
	|                             `- src
	|
	`- build - configs - <board-name>
```

<a id="architecture"></a>
## 아키텍처

여기에는 *CPU* 및 *칩* 아키텍처가 포함됩니다.  

### CPU

```
	.
	`- os - arch - <cpu-familyname> .- include - <cpu-archname>
	                          |
	                          `- src - <cpu-archname>
```
**CPU**에는 경로에 *\<cpu-archname\>*와 함께 *\<cpu-familyname\>*가 있습니다.  
- cpu-family : arm, mips(아직 지원되지 않음), ...
- cpu 아키텍처: armv7-r, armv7-m, 공통, ...

*os/arch/\<cpu-familyname\>* 아래에 소스코드를 추가한 후,  
새로운 아키텍처 선택을 제공하려면 *os/arch*의 **Kconfig**도 아래와 같이 변경해야 합니다.
```
choice
	prompt "CPU Architecture"
	default ARCH_ARM

config ARCH_ARM
	bool "ARM"
	---help---
		The ARM architectures

config ARCH_<CPU-FAMILYNAME>
	bool "<CPU-FAMILYNAME>"
	---help---
		The <CPU-FAMILYNAME> architecture

endchoice

config ARCH
	string
	default "arm"        if ARCH_ARM
	default "<cpu-familyname>  if ARCH_<CPU-FAMILYNAME>

source arch/arm/Kconfig
source arch/<cpu-familyname>/Kconfig
```

>**참고**
>`config ARCH`의 값은 `<cpu-familyname>`의 폴더 이름과 동일해야 합니다.
>현재 TizenRT는 *armv7-m*, *armv7-r* 및 *common* 폴더가 있는 ARM 아키텍처만 지원합니다.

### 칩

```
	.
	`- os - arch - <cpu-familyname> .- include .- <cpu-archname>
	                          |          |
	                          |          `- <chip-name>
	                          |
	                          `- src .- <cpu-archname>
	                                 |
	                                 `- <chip-name>
```

현재 TizenRT에는 칩 아키텍처용 *bcm4390x*, *s5j* 및 *tiva* 폴더가 포함되어 있습니다.  
*src* 및 *include* 폴더 아래에 새 칩 폴더를 추가해야 합니다.  
*src* 아래의 `<chip-name>` 폴더에는 구성 및 컴파일을 제공하기 위해 **Make.defs** 및 **Kconfig**가 포함되어야 합니다.

*os/arch/\<cpu-familyname\>*의 Kconfig는 CPU처럼 지원되는 TizenRT 중에서 칩 선택을 지원합니다.
```
choice
	prompt "<CPU-FAMILYNAME> chip selection"
	default ARCH_CHIP_S5J

config ARCH_CHIP_S5J
	bool "Samsung S5J"
	---help---
		Samsung IoT SoC architectures (ARM Cortex R)

config ARCH_CHIP_<CHIP-NAME>
	bool "xxxxx"

endchoice

config ARCH_CHIP
	string
	default "s5j"              if ARCH_CHIP_S5J
	default "<chip-name>"      if ARCH_CHIP_<CHIP-NAME>

if ARCH_CHIP_S5J
source arch/<cpu-familyname>/src/s5j/Kconfig
endif
if ARCH_CHIP_<CHIP-NAME>
source arch/<cpu-familyname>/src/<CHIP-NAME>/Kconfig
```

\<cpu-familyname\>와 마찬가지로 `config ARCH_CHIP` 값도 폴더 이름과 동일해야 합니다.

<a id="board"></a>
## 보드

```
	.
	|- os .- arch
	      |
	      `- board - <board-name> .- include
	                              |
	                              `- src
```

`<board-name>` 폴더를 만든 후 *include* 폴더에 헤더를 넣고, *src* 폴더에 소스코드를 넣어줍니다.  
CPU 및 칩 선택과 동일하게 *os/board*의 **Kconfig**에 의해 새 보드가 선택됩니다.

```
choice
	prompt "Select target board"
	default ARCH_BOARD_SIDK_S5JT200

config ARCH_BOARD_LM3S6965EK
	bool "Stellaris LM3S6965 Evaluation Kit"

config ARCH_BOARD_<BOARD-NAME>
	bool "XXX"

endchoice

config ARCH_BOARD
	string
	default "lm3s6965-ek"        if ARCH_BOARD_LM3S6965EK
	default "<board-name>"       if ARCH_BOARD_<BOARD-NAME>

if ARCH_BOARD_LM3S6965EK
source board/lm3s6965-ek/Kconfig
endif
if ARCH_BOARD_<BOARD-NAME>
source board/<board-name>/Kconfig
endif
```

<a id="how-to-include-arch-and-board-header-files"></a>
## 아치 및 보드 헤더 파일을 포함하는 방법

빌드 시 Makefile은 선택한 칩 및 보드 폴더를 선택한 CPU 폴더에 동적으로 연결합니다.  
빌드 후 *os/arch/\<cpu-familyname\>/src* 폴더에서 *chip* 및 *board* 폴더를 찾을 수 있습니다.

또한 이때 *include* 폴더는 *os/include/arch* 폴더에 연결되어 공용 API 및 정의를 노출합니다.

예를 들어 *os/arch/arm/include/bcm4390x/chip.h*에 정의된 *bcm4390x* 칩 API를 사용하려면 `#include <arch/chip/chip.h>`가 필요합니다.  
*os/board/artik05x/artik055_alc5658_i2c.h*의 경우 `#include <arch/board/artik055_alc5658_i2c.h>`를 사용할 수 있습니다.

>**Note**
>링크된 CPU 폴더에서 칩 및 보드 폴더의 링크를 해제하려면 *make distclean* 명령을 사용할 수 있습니다.

<a id="configs"></a>
## 구성

*configs* 폴더의 변경 사항에는 빌드 옵션 및 플래시 작업이 포함됩니다.

1. 보드 이름으로 새 폴더를 추가합니다.
	```
	mkdir build/configs/<board-name>
	```

2. *scripts* 폴더를 추가하고 링커 스크립트 파일을 추가합니다.
	```
	mkdir build/configs/<board-name>/scripts
	```

3. \<board-name\> 폴더 아래에 기본 프로그램 이름을 나타내는 새 폴더를 추가합니다.
	```
	mkdir build/configs/<board-name>/<program-name>
	```

4. \<program-name\> 안에 *defconfig* 및 *Make.defs*를 넣으세요.

### defconfig

폴더 이름을 나타내는 구성 집합입니다.  
configure.sh 실행 시 .config로 이동되어 TizenRT를 빌드하는 데 사용됩니다.

### Make.defs

포함 경로, ARM 빌드 옵션, 링커 스크립트 이름 등의 설정과 같은 빌드 옵션 세트입니다.  
"make download xx" 명령도 지원하는 DOWNLOAD 정의가 있습니다.

### 링커 변수

링커 스크립트에는 메모리 구성 정보가 있습니다.  
파일 경로 : *build/configs/<BOARD_NAME>/scripts/<SCRIPTS_NAME>.ld* 

SECTION 정보 외에도 다음 변수를 추가하여 다음을 표시합니다.  
1. `_sbss`: .bss 섹션 시작
2. `_ebss`: .bss 섹션의 끝+1
3. `_sidle_stack`: 유휴 스택 시작
4. `_sint_heap`: 내부 RAM 영역에서 힙 시작
5. `_sext_heap`: 외부 RAM 영역에서 힙 시작

예를 들어,  

1. bss 영역 뒤의 나머지 RAM 영역은 유휴 스레드 스택 및 내부 힙 영역(`build/configs/sidk_s5jt200/scripts/ld_s5jt200_flash.script`)으로 설정할 수 있습니다.
```
	.bss : {
		_sbss = ABSOLUTE(.);
		*(.bss .bss.*)
		*(.gnu.linkonce.b.*)
		*(COMMON)
		. = ALIGN(4);
		_ebss = ABSOLUTE(.);
		_sidle_stack = ABSOLUTE(.);
		. = . + CONFIG_IDLETHREAD_STACKSIZE ;
		/* Heap start address in internal RAM */
		_sint_heap = ABSOLUTE(.);
	} > sram
```
2. Idle 스레드 스택 및 내부 힙 영역은 메모리 레이아웃(`build/configs/rtl8720e/scripts/rlx8720e_img2.ld`)에 따라 다른 영역(bss 끝 제외)에 설정되어야 합니다.
```
	.ram_image2.bss (NOLOAD):
	{
		__bss_start__ = .;
		*(.bss*)
		*(COMMON)
		__bss_end__ = .;
	} > KM4_BD_RAM

	.psram_heap.start (NOLOAD):
	{
		/* Heap start address in external RAM */
		_sext_heap = ABSOLUTE(.);
	} > KM4_BD_PSRAM

	/* Heap start address in internal RAM */
	_sint_heap = ABSOLUTE(ORIGIN(KM4_HEAP_EXT));
	_sidle_stack = ABSOLUTE(ORIGIN(KM4_MSP_RAM_NS) + LENGTH(KM4_MSP_RAM_NS)) - CONFIG_IDLETHREAD_STACKSIZE;
```

위의 정보 외에도 모든 구성(`build/configs/sidk_s5jt200/hello/Make.defs`)에 대해 보드 `Make.defs`에서 아래 명령문을 사용하여 CONFIG_IDLETHREAD_STACKSIZE를 링커 스크립트로 내보냅니다.  
```
LDFLAGS += --defsym=CONFIG_IDLETHREAD_STACKSIZE=$(CONFIG_IDLETHREAD_STACKSIZE)
```
