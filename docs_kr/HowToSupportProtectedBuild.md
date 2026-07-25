# Protected 빌드 가이드

## 목차
- [플랫 빌드 개요](#overview-of-flat-build)
- [보호된 빌드 개요](#overview-of-protected-build)
- [새로운 칩셋에서 보호된 빌드를 지원하는 단계](#steps-for-supporting-protected-build-in-a-new-chipset)
	- [전제조건](#prerequisites)
	- [링커 스크립트](#linker-scripts)
	- [사용자 공간 초기화](#userspace-initialization)
	- [MPU 초기화](#mpu-initialization)
	- [커널 및 사용자 힙 할당](#allocation-of-kernel-and-user-heaps)
	- [메이크파일 변경](#makefile-changes)
	- [보호된 빌드를 위한 새 구성 만들기](#creating-a-new-configuration-for-protected-build)
	- [바이너리 다운로드](#binary-download)
- [사용 가능한 API](#available-apis)


<a id="overview-of-flat-build"></a>
## 플랫 빌드 개요
- TizenRT 플랫 빌드에서는 모든 사용자 및 커널 코드를 포함하는 단일 바이너리 파일이 생성됩니다.
- 사실 플랫 빌드에서는 사용자-커널 구분이 없습니다.
- 플랫 빌드는 코드의 모든 부분이 메모리 맵의 모든 주소에 액세스할 수 있는 플랫 주소 공간을 제공합니다.
- TizenRT가 새 칩셋으로 이식되는 경우 플랫 빌드 시나리오에 대한 이식 활동이 수행됩니다.
- [새 보드를 추가하는 방법](HowToAddnewBoard.md) 문서에서는 새 칩셋에 대한 TizenRT 플랫 빌드 포팅에 대한 지침을 제공합니다.
- 플랫 빌드가 성공적으로 검증되면 이 문서에 제공된 세부 정보를 사용하여 칩셋에서 보호된 빌드를 지원할 수 있습니다.


<a id="overview-of-protected-build"></a>
## 보호된 빌드 개요
- 보호된 빌드에서는 커널 바이너리와 사용자 바이너리라는 두 개의 별도 바이너리가 생성됩니다.
- 빌드 프로세스 후에 커널 바이너리는 tinyara.bin라는 이름으로 생성되고 사용자 바이너리는 <build/output/bin> 경로에 - tinyara_user.bin으로 생성됩니다.
- 커널 바이너리는 아키텍처(ARCH), 코어 커널 모듈(SCHED), C-Lib 및 메모리 관리자(MM), 파일 시스템(FS) 및 드라이버 등의 모듈로 구성됩니다.
- 사용자 바이너리에는 애플리케이션, C-Lib 및 메모리 관리자(MM), 프레임워크 및 외부 모듈과 같은 모듈이 포함됩니다.
- C-lib와 Memory Manager가 커널 공간과 사용자 공간에 중복되어 있습니다.
- A 메모리용 코드 메모리(플래시) 영역과 데이터 메모리(RAM) 영역은 사용자와 커널에서 사용할 수 있도록 각각 두 부분으로 분할됩니다.
- 커널과 사용자 바이너리는 별도의 코드 메모리 영역에 배치되며 런타임 중에는 별도의 데이터 메모리 영역을 사용합니다.
- 아키텍처가 MPU(Memory Protection Unit)를 지원하는 경우 커널 로직은 특권 모드에서 실행되고 사용자 로직은 비특권 모드에서 실행됩니다. MPU는 권한 없는 모드에서 모든 커널 메모리 영역에 대한 액세스를 금지합니다.
- 프로세서가 권한 모드에서 실행되면 모든 메모리 영역에 액세스할 수 있습니다. 프로세서가 비특권 모드에서 실행 중이면 부팅 중 커널에 의해 구성된 사용자 공간 메모리 영역에만 액세스할 수 있습니다.
- 사용자 공간 코드는 시스템 호출(하향 통신)을 통해서만 커널 서비스에 액세스할 수 있습니다.
- 메모리 영역과 같은 사용자 공간 및 일부 사용자 공간 API에 대한 정보를 커널에 공유해야 합니다. 이 정보는 빌드 시간 동안 커널 바이너리에 연결되는 사용자 공간 개체라는 특수 데이터 구조에 저장됩니다. 커널은 사용자 공간 개체에서 사용할 수 있는 사용자 공간 API에 대한 함수 포인터를 사용하여 필요한 사용자 공간 기능을 호출할 수 있습니다.


<a id="steps-for-supporting-protected-build-in-a-new-chipset"></a>
## 새 칩셋에서 보호 빌드를 지원하기 위한 단계
다음 섹션에서는 새 칩셋에서 보호된 빌드를 지원하는 것과 관련된 일부 단계/코드 변경에 대해 자세히 설명합니다.

<a id="prerequisites"></a>
### 전제 조건
- TizenRT 플랫 빌드는 대상 칩셋 보드에서 성공적으로 포팅되고 테스트되어야 합니다.
- 개발자는 시작 주소 및 크기와 함께 보드에서 지원되는 메모리 유형에 대한 정보를 가지고 있어야 합니다. 플래시 메모리의 파티션 정보도 알아야 합니다. 이러한 세부 정보는 칩셋의 데이터 시트, 프로젝트 구성 파일, 링커 또는 다운로드 스크립트 또는 칩 소스 코드의 일부로 언급된 메모리 맵에서 찾을 수 있습니다.

<a id="linker-scripts"></a>
### 링커 스크립트

- 플랫 빌드의 경우 단일 링커 스크립트 ld.script는 대상에 있는 메모리 영역의 위치와 크기를 설명하고 바이너리의 다양한 섹션을 다양한 메모리 영역에 배치하고 연결하는 방법도 설명합니다.
- 보호된 빌드의 경우 이 기능을 수행하려면 세 개의 링커 스크립트가 필요합니다.
	- memory.ld : 대상의 메모리 영역 위치와 크기를 설명합니다.
	- kernel-space.ld : 커널 바이너리 섹션을 특정 메모리 영역에 할당하는 데 사용됩니다.
	- user-space.ld : 이는 사용자 바이너리 섹션을 특정 메모리 영역에 할당하는 데 사용됩니다.
- kernel-space.ld 및 user-space.ld는 memory.ld에 정의된 메모리 영역을 사용합니다.
- 이 세 파일은 모두 보호된 빌드용으로 생성되어 build/configs/CHIPSET/scripts 폴더에 배치되어야 합니다.

#### Memory.ld
- 이 스크립트는 커널 및 사용자 링커 스크립트에서 사용할 코드 및 데이터 메모리 영역을 정의합니다.
- 일반적으로 보드에는 두 가지 유형의 메모리가 포함됩니다. 첫 번째는 코드를 포함할 플래시 메모리이고 두 번째는 실행 중 데이터, 스택 및 힙에 사용되는 RAM 메모리입니다.
- 개발자는 이러한 메모리의 시작 주소와 크기를 알아내야 합니다. 이 정보는 데이터시트, 구성 파일, 플랫 빌드의 다운로드 및 링커 스크립트, CHIPSET의 코드베이스에 정의된 메모리 맵 등의 소스에서 찾을 수 있습니다.
- 코드 메모리는 커널과 사용자 코드 메모리로 분할되어야 합니다. 마찬가지로 데이터 메모리도 커널과 사용자 데이터 메모리로 분할되어야 합니다.
- 이 두 메모리는 모두 50:50 공유 기준으로 사용자와 커널 간에 분할되는 것이 좋습니다.
- 당연히 다양한 메모리 영역의 크기는 사용자와 커널의 크기 요구 사항에 따라 조정될 수 있습니다.
- 이러한 파티션을 만든 후 개발자는 memory.ld 파일의 적절한 속성과 함께 다음과 같이 4개의 메모리 영역을 정의해야 합니다.
	- kflash(커널 바이너리용)
	- uflash(사용자 바이너리용)
	- ksram(커널 바이너리용)
	- usram(사용자 바이너리용)
- 아래 조각은 총 플래시가 2048K이고 RAM이 640K 바이트인 보드에 대한 memory.ld 파일의 내용을 보여줍니다. 이는 의도한 CHIPSET에 대한 'memory.ld' 스크립트를 생성하기 위한 참조로 사용될 수 있습니다.

```
/* 2048Kb FLASH */

	kflash (rx)      : ORIGIN = 0x08000000, LENGTH = 1024K
	uflash (rx)      : ORIGIN = 0x080FA000, LENGTH = 1024K

/* 640Kb of contiguous SRAM */

	ksram (rwx)      : ORIGIN = 0x20000000, LENGTH = 320K
	usram (rwx)      : ORIGIN = 0x2004E200, LENGTH = 320K

        __kflash_segment_start__  = ORIGIN(kflash);
        __kflash_segment_size__   = LENGTH(kflash);
        __uflash_segment_start__  = ORIGIN(uflash);
        __uflash_segment_size__   = LENGTH(uflash);
        __ksram_segment_start__   = ORIGIN(ksram);
        __ksram_segment_size__    = LENGTH(ksram);
        __usram_segment_start__   = ORIGIN(usram);
        __usram_segment_size__    = LENGTH(usram);

```

- __uflash_segment_start__ 링커 변수를 정의하는 것은 필수입니다. 이는 사용자 공간 개체의 주소를 얻기 위해 코드에서 사용됩니다.
- 사용자는 CONFIG_FLASH_PART_XXXX 구성의 플래시 파티션 정보를 기반으로 memory.ld 파일을 자동 생성하는 CONFIG_AUTOGEN_MEMORY_LDSCRIPT 구성을 활성화할 수도 있습니다.

#### 커널 공간.ld
- 플랫 빌드에 사용되는 ld.script 파일의 복사본을 만들어 이 파일을 만듭니다.
- 복사한 후 모든 메모리 영역 정의를 제거하고 kernel-space.ld 파일에 '섹션' 정의만 유지합니다.
- 모든 "플래시" 메모리 영역을 "kflash"로 바꾸고 "ram" 또는 "sram" 메모리 영역을 "ksram"으로 바꿉니다.
- 아래 조각은 텍스트 섹션이 kflash에 할당되고 데이터 섹션이 ksram 영역에 할당되는 샘플을 보여줍니다.

```
SECTIONS
{
	.text : {
		_stext_flash = ABSOLUTE(.);
		....
		....
		_etext_flash = ABSOLUTE(.);
	} > kflash
	/* Here in comparison to ld.script for flat build, 'flash' memory region should be replaced with 'kflash' memory region. */
	....
	.data : {
		_sdata = ABSOLUTE(.);
		....
		_edata = ABSOLUTE(.);
	} > ksram
	/* Here in comparison to ld.script for flat build, 'sram' memory region should be replaced with 'ksram' memory region. */
}
```

#### user-space.ld
- 이 링커 스크립트는 사용자 바이너리를 빌드하는 동안 사용됩니다. 이 스크립트에 대한 참조는 os/board/common/userspace/Makefile.에서 찾을 수 있습니다. 
- 모든 "플래시" 메모리 영역을 "uflash"로 바꾸고 "ram" 또는 "sram" 또는 "ksram" 메모리 영역을 "usram"으로 바꿉니다.
- 이 스크립트에는 kernel-space.ld와 비교하여 두 가지 주요 추가 사항이 있습니다.
1. UserSpace 개체 - 이는 사용자 공간 바이너리의 시작 부분에 배치되어야 합니다. 이는 ```.userspace``` 속성을 사용하여 수행됩니다. 아래 스니펫에 표시된 대로 이 목적을 위해 user-space.ld 파일의 텍스트 섹션 앞에 새 섹션이 도입되었습니다. 사용자 공간 힙에 대한

```
.userspace : {
	*(.userspace)
} > uflash
```
2. A 포인터는 텍스트 섹션의 시작 부분에 배치되어야 합니다. 이는 MM(메모리 관리자) 모듈에서 할당을 수행하는 데 사용됩니다. 따라서 사용자 힙 포인터(.usrheapptr)는 다음과 같이 사용자 메모리의 텍스트 섹션에 추가되어야 합니다.

```
.text : {
	_stext_flash = ABSOLUTE(.);
	*(.usrheapptr)		/* User heap pointer added in the .text user section */
	....
	_etext_flash = ABSOLUTE(.);
} > uflash
```

<a id="userspace-initialization"></a>
### 사용자 공간 초기화
- 폴더 경로 <os/arch/arm/src/CHIPSET>에 "CHIPSET_userspace.c" 파일을 생성하고 파일에 아래와 같이 API를 정의합니다.
- A 헤더 파일 "CHIPSET_userspace.h"도 <os/arch/arm/src/CHIPSET> 경로에 생성되어야 하며 아래 주어진 API를 선언해야 합니다.
- 이 API의 기능은 모든 사용자 공간 .bss를 지우고 모든 사용자 공간 .data를 초기화하는 것입니다.
- 사용자 공간 데이터 섹션이 플래시 메모리에서 지정된 RAM 메모리 영역으로 복사됩니다.
- 플래시가 XIP를 지원하지 않는 경우 코드 섹션도 플래시에서 지정된 RAM 영역으로 복사해야 합니다.
- 코드, 데이터 및 bss 섹션의 시작 주소와 크기는 USERSPACE 개체에서 찾을 수 있습니다.
- "CHIPSET_start.c"에 있는 '__start()' API에서 CHIPSET_userspace() API를 호출합니다(경로: <os/arch/arm/src/CHIPSET> ).

```
	void CHIPSET_userspace(void) {
		  ....
		/* Clear all of user-space .bss */

		dest = (uint8_t *)USERSPACE->us_bssstart;	/*STORING THE START ADDRESS OF .bss section */
		end  = (uint8_t *)USERSPACE->us_bssend;		/*STORING THE END ADDRESS OF .bss section */

		while (dest != end) {
		  *dest++ = 0;					/*Initialization*/
		}

		/* Initialize all of user-space .data */

		src  = (uint8_t *)USERSPACE->us_datasource;	/*STORING THE VALUE WITH WHICH .data section is to be initialized with */
		dest = (uint8_t *)USERSPACE->us_datastart;	/*STORING THE START ADDRESS OF .data section */
		end  = (uint8_t *)USERSPACE->us_dataend;	/*STORING THE END ADDRESS OF .data section */

		while (dest != end) {
		  *dest++ = *src++;				/*Initialization */
		}
		....
	}
```

<a id="mpu-initialization"></a>
### MPU 초기화
- 이 단계에서는 권한 모드에서 전체 메모리 맵에 대한 기본 액세스를 제공하도록 MPU를 구성합니다.
- 또한 사용자 공간 코드와 데이터 영역을 구성하고 MPU를 활성화합니다.
- MPU 지역 구성은 아치 특정 코드에서 사전 정의된 API 중 하나를 사용합니다.
- 예를 들어 mpu_userflash, mpu_userintsram API를 사용하여 사용자 플래시 및 램 영역을 구성할 수 있습니다.
- 이러한 API와 기타 여러 API는 arch/arm/src/armv7m/ 폴더의 mpu.h 파일에 제공됩니다.
- 구성할 메모리 유형에 따라 이 파일에서 다른 API를 사용할 수 있습니다.
- MPU 영역 구성에는 각 영역의 시작 및 크기와 할당되어야 하는 영역 번호가 필요합니다.
- A 파일 'CHIPSET_mpuinit.c'는 폴더 경로 <os/arch/arm/src/CHIPSET>에 생성되어야 합니다.
- A 헤더 파일 'CHIPSET_mpuinit.h'도 사용된 API를 선언하기 위해 <os/arch/arm/src/CHIPSET> 경로에 생성되어야 합니다.
- 파일에서 먼저 사용자 공간 메모리 영역을 mpu_region_info 구조의 배열로 정의합니다. 다음 코드 조각은 샘플 정의를 보여줍니다.

```
	#if defined(CONFIG_BUILD_PROTECTED)
	const struct mpu_region_info regions_info[] = {
	{
		&mpu_userflash, (uintptr_t)__uflash_segment_start__, (uintptr_t)__uflash_segment_size__, MPU_REG_USER_CODE,
	},
	{
		&mpu_userintsram, (uintptr_t)__usram_segment_start__, (uintptr_t)__usram_segment_size__, MPU_REG_USER_DATA,
	},
	};
	#endif
```
- 두 번째로 MPU를 초기화하고 구성하기 위한 API를 작성해야 합니다. 아래 코드 조각에 샘플이 나와 있습니다.

```
void CHIPSET_mpuinitialize(void)
{
	....
	for (i = 0; i < (sizeof(regions_info) / sizeof(struct mpu_region_info)); i++) {
		lldbg("Region = %u base = 0x%x size = %u\n", regions_info[i].rgno, regions_info[i].base, regions_info[i].size);
		regions_info[i].call(regions_info[i].rgno, regions_info[i].base, regions_info[i].size);
	}
	....

	mpu_control(true, false, true);		/*ENABLE the MPU : this enables priviledged access to default memory map */
}
```

- 'CHIPSET_start.c'에 있는 '__start()'에서 CHIPSET_mpuinitialize API를 호출합니다.

<a id="allocation-of-kernel-and-user-heaps"></a>
### 커널 및 사용자 힙 할당
- Kernel 및 사용자 힙은 OS 시작 단계에서 up_allocate_kheap 및 up_allocate_heap API를 호출하여 초기화됩니다.
- 이러한 API에 대한 기본 구현은 os/arch/arm/src/common/up_allocateheap.c.에서 제공됩니다.
- 커널 힙은 일반적으로 유휴 스택의 끝에서 시작하여 커널 램 영역의 나머지 공간 전체에 걸쳐 있습니다.
- 사용자 힙은 사용자 bss 섹션의 끝에서 시작하여 사용자 ram 영역의 나머지 공간 전체에 걸쳐 있습니다.
- 그러나 칩셋의 메모리 구성이 기본 구성과 다르거나 공급업체가 커널 및 사용자 힙을 다른 위치 또는 크기로 초기화하려는 경우 공급업체는 CHIPSET_allocateheap.c와 같은 칩셋별 파일에 새 버전의 API를 구현하고 up_allocate.c 대신 이 새 파일을 빌드할 수 있습니다. 파일.
- 공급업체는 API의 프로토타입이 새로운 구현에서 변경되지 않도록 주의해야 합니다.
- 예를 들어 아래 주어진 API는 커널 공간 힙 할당을 위해 기록됩니다.

```
	#if defined(CONFIG_BUILD_PROTECTED) && defined(CONFIG_MM_KERNEL_HEAP)
	void up_allocate_kheap(FAR void **heap_start, size_t *heap_size){
		//The kernel heap will start at the end of the idle stack
		*heap_start = (FAR void *)(g_idle_topstack & ~(0x7));

		//The kernel heap will extend from the end of idle stack to the start of user ram region
		//The start address of the user ram region is obtained from the variables defined in memory.ld linker script
		*heap_size = (uint32_t)((uintptr_t)__usram_segment_start__) - (uint32_t)(*heap_start);
	}
	#endif
```

- 사용자 공간 힙 할당을 위한 API는 다음과 같이 수정되어야 합니다.

```
	void up_allocate_heap(FAR void **heap_start, size_t *heap_size){
	#if defined(CONFIG_BUILD_PROTECTED) && defined(CONFIG_MM_KERNEL_HEAP)

		//The user heap will extend from the end of bss section to the end of user ram region
		// The start address and the size of the user ram region is stored in variables defined in memory.ld script
		uintptr_t user_end = (uint32_t)__usram_segment_start__ + (uint32_t)__usram_segment_size__;
		//The user heap will start at the end of the .bss section
		uintptr_t ubase = (uintptr_t)USERSPACE->us_bssend;
		size_t usize = (uint32_t)user_end - ubase;
		....
	#endif
	}
```

<a id="makefile-changes"></a>
### Make파일 변경
- <os/arch/arm/src/CHIPSET/Make.defs> 경로의 Make.def에 다음을 추가해야 합니다.

```
	ifeq ($(CONFIG_BUILD_PROTECTED),y)
	CHIP_CSRCS += CHIPSET_userspace.c
	endif
	ifeq ($(CONFIG_ARMV7M_MPU),y)
	CHIP_CSRCS += CHIPSET_mpuinit.c
	endif
```

<a id="creating-a-new-configuration-for-protected-build"></a>
### 보호된 빌드에 대한 새 구성 만들기
- 모든 칩셋에는 경로 build/config/CHIPSET_NAME. 아래에 hello 구성이 있습니다.
- "hello" 폴더의 내용을 "hello_protected"라는 새 폴더에 복사합니다.
- menuconfig를 사용하여 아래 구성을 수정한 후 os/.config 파일을 defconfig라는 이름의 configs 폴더에 복사합니다.

#### Menuconfig 변경
- ```build setup -> build configuration -> Memory organization -> TinyAra protected buil```를 통해 CONFIG_BUILD_PROTECTED 활성화
- ```build setup -> build configuration -> Two pass build```를 통해 CONFIG_BUILD_2PASS 활성화 
- CONFIG_PASS1_TARGET를 ```build setup -> build configuration -> Pass one target```를 통해 ```all```로 설정
- CONFIG_PASS1_OBJECT를 ```build setup -> build configuration -> Pass one object```를 통해 ```null```로 설정
- ```Chip Selection -> MPU support```부터 CONFIG_ARMV7M_MPU 선택
- CONFIG_ARMV7M_MPU_NREGIONS를 ```Chip Selection -> Number of MPU regions```를 통해 ```8```로 설정
- ```Memory Management -> Support a protected, kernel heap```부터 CONFIG_MM_KERNEL_HEAP 선택
- ```System Call -> System call support```부터 CONFIG_LIB_SYSCALL 선택
- CONFIG_SYS_RESERVED를 ```System Call -> System call support -> Number of reserved system calls```를 통해 ```8```로 설정
- CONFIG_SYS_NNEST를 ```System Call -> System call support -> Number of nested system calls```를 통해 ```2```로 설정

#### Make.defs 변경
- <build/config/CHIPSET_NAME/hello_protected> 경로에 있는 make.defs 파일은 빌드 프로세스 중에 사용되는 여러 정의를 제공합니다. 정의 중 하나는 커널 바이너리를 연결하는 데 사용되는 링커 스크립트에 대한 (ARCHSCRIPT)입니다.
- 보호된 빌드 지원을 위해 아래 두 개의 링커 스크립트를 사용하도록 이 파일을 수정하세요.

```
MEM_LDSCRIPT = memory.ld
KSPACE_LDSCRIPT = kernel-space.ld
....
ARCHSCRIPT = -T$(TOPDIR)/../build/configs/$(CONFIG_ARCH_BOARD)/scripts/$(MEM_LDSCRIPT)
ARCHSCRIPT += -T$(TOPDIR)/../build/configs/$(CONFIG_ARCH_BOARD)/scripts/$(KSPACE_LDSCRIPT)
```

<a id="binary-download"></a>
### 바이너리 다운로드
- 다양한 칩셋에는 다운로드 목적으로 다른 구성, 스크립트 또는 다른 GUI 기반 도구가 있습니다.
- 보호된 빌드에서는 두 개의 바이너리(커널 및 사용자)를 다운로드해야 합니다. 따라서 이를 수용하도록 다운로드 프로세스를 수정해야 합니다.
- 다운로드 프로세스에서는 'defconfig' 파일에 정의된 CONFIG_FLASH_PART_NAME가 제공하는 "앱" 파티션에서 사용자 바이너리가 플래시되는지 확인해야 합니다.

<a id="available-apis"></a>
## 사용 가능한 API
Protected 빌드의 주요 목적은 분리를 통해 사용자로부터 커널을 보호하는 것이므로 사용자 애플리케이션은 sched_gettcb API와 같은 커널 내부 작업을 관리하는 일부 커널 API를 사용할 수 없습니다.
사용자가 보호된 빌드와 함께 사용할 수 있는 플랫폼 API는 syscall, libc, 프레임워크 및 외부 폴더에 정의되어 있습니다.  
보호된 빌드에서 API가 사용 가능한지 여부를 확인하려면 목록을 찾으십시오.

- [libc API 목록](https://github.com/Samsung/TizenRT/blob/master/lib/libc/libc.csv)
- [수학 API 목록](https://github.com/Samsung/TizenRT/blob/master/lib/libc/math.csv)
- [syscall API 목록](https://github.com/Samsung/TizenRT/blob/master/os/syscall/syscall.csv)
- [프레임워크의 헤더](https://github.com/Samsung/TizenRT/tree/master/framework/include)
- [외부 헤더](https://github.com/Samsung/TizenRT/tree/master/external/include)
