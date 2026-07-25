# TizenRT에서 MPU 영역 사용에 대한 개발자 가이드

## 목차
- [개요](#overview)
- [MPU를 활성화하는 방법](#how-to-enable-mpu)
- [MPU 제어 방법](#how-to-control-mpu)
- [MPU 플랫폼 용도](#mpu-usages-of-platform)
- [MPU 영역의 샘플 메모리 레이아웃](#sample-memory-layout-of-mpu-regions)
- [충수](#appendix)

<a id="overview"></a>
## 개요
- MPU의 주요 목적은 권한 있는 액세스 수준과 권한 없는 액세스 수준에서 다양한 액세스 권한, 속성 등을 정의하여 메모리 영역을 보호하는 것입니다.
- TizenRT는 사용 가능한 MPU 영역을 아래의 특정 사용 사례로 나눕니다.
	- 보드 특정
	- 애플리케이션 바이너리
	- 공통 바이너리
	- 스택 오버플로
- 정적 영역 번호는 위의 각 MPU 유형에 할당되어 구성됩니다.

<a id="how-to-enable-mpu"></a>
## MPU 활성화 방법

- 제공된 MPU는 칩에서 지원되며, MPU 영역(ARMv7M 아키텍처의 경우)을 구성하려면 다음 구성을 활성화해야 합니다.
```
	------------------------------------------------------------------------------------------------------------------------------
	CONFIG FLAG			VALUE		DESCRIPTION		MENU PATH
	------------------------------------------------------------------------------------------------------------------------------
	CONFIG_ARMV7M_MPU		y		MPU support		Chip Selection -> MPU Support
	CONFIG_ARMV7M_MPU_NREGIONS	16		Number of MPU regions	Chip Selection -> MPU Support -> Number of MPU Regions
	------------------------------------------------------------------------------------------------------------------------------
```

- 마찬가지로 ARMv8M 아키텍처의 경우 아래 MPU 구성을 활성화해야 합니다.
```
	------------------------------------------------------------------------------------------------------------------------------
	CONFIG FLAG			VALUE		DESCRIPTION		MENU PATH
	------------------------------------------------------------------------------------------------------------------------------
	CONFIG_ARMV8M_MPU		y		MPU support		Chip Selection -> MPU Support
	CONFIG_ARMV8M_MPU_NREGIONS	8		Number of MPU regions	Chip Selection -> MPU Support -> Number of MPU Regions
	------------------------------------------------------------------------------------------------------------------------------
```

<a id="how-to-control-mpu"></a>
## MPU 제어 방법
1. **MPU 사용 개요**
	- MPU 영역을 해당 메모리 세그먼트에 대한 논리적 매핑은 공통 mpu.h 헤더 파일(os/include/tinyara/mpu.h)의 아래 구조를 사용하여 정적으로 수행됩니다.
	```c
	struct mpu_usages_s {
	        uint8_t nregion_board_specific;
	        uint8_t nregion_common_bin;
	        uint8_t nregion_app_bin;
	        uint8_t nregion_stackovf;
	        uint8_t max_nregion;
	};
	```
	- 이 구조는 각 유형의 mpu 영역(보드 특정, 응용 프로그램 바이너리, 공통 바이너리 등)에 사용되는 영역 번호 범위를 보유하며 아래 API를 사용하여 시스템 부팅 중에 초기화됩니다.
	```c
	void mpu_region_initialize(struct mpu_usages_s *mpu)

	offset += NUM_APP_REGIONS;
	mpu->nregion_xxx_xx = offset;
	```
2. **보드별 MPU 지역 예약**
	- 보드 특정 목적을 위해 소수의 지역을 지원하기 위해 아래 API가 제공되어 첫 번째 *num* 수의 지역을 예약합니다.
	```c
	void mpu_set_nregion_board_specific(uint8_t num);
	```
	위 함수는 보드별 MPU 영역이 구성된 후 보드 초기화 중에 호출되어야 합니다. TizenRT MPU 영역은 보드 MPU 영역 *num*개 이후에 구성됩니다.

3. **TizenRT MPU 영역을 실제 MPU 하드웨어**로 등록하는 방법
	- 아래 기능 목록은 TizenRT에서 MPU 영역을 MPU 하드웨어에 등록하는 데 사용할 수 있습니다.
		- mpu_xxxx 형식의 여러 기능은 MPU 영역의 일회성 구성을 위해 아키텍처별 mpu.h 파일(예: os/arch/arm/src/armv7-m/mpu.h)에서 사용할 수 있습니다.
			- 예: 사용자 플래시 및 사용자 내부 SRAM에 대한 MPU 영역을 구성하는 경우 아래 기능을 사용할 수 있습니다.
			```c
			/****************************************************************************
			 * Name: mpu_user_flash
			 *
			 * Description:
			 *   Configure a region for user program flash
			 *
			 * Params:
			 *  region : MPU region number
			 *  base   : MPU region base address
			 *  size   : MPU region size
			 ****************************************************************************/
			static inline void mpu_userflash(uint32_t region, uintptr_t base, size_t size);
			```
			```c
			/****************************************************************************
			 * Name: mpu_userintsram
			 *
			 * Description:
			 *   Configure a region as user internal SRAM
			 *
			 * Params:
			 *  region : MPU region number
			 *  base   : MPU region base address
			 *  size   : MPU region size
			 ****************************************************************************/
			static inline void mpu_userintsram(uint32_t region, uintptr_t base, size_t size);
			```
			- 다음은 MPU 영역 구성에 대한 기능을 제공하는 TizenRT의 ARM 아키텍처 관련 mpu.h 파일에 대한 링크입니다.
				- [ARMv7-M MPU 함수](../os/arch/arm/src/armv7-m/mpu.h)
				- [ARMv7-R MPU 함수](../os/arch/arm/src/armv7-r/mpu.h)
				- [ARMv8-M MPU 함수](../os/arch/arm/src/armv8-m/mpu.h)
		- MPU 영역의 /customized 구성을 반복하려면 아래 함수를 사용하여 3단계 프로세스를 적용해야 합니다.
			1. 첫 번째 단계에서는 아래 API를 사용하여 구성할 지역 번호를 가져옵니다.
			```c
			uint8_t nregion = mpu_get_nregion_info(MPU_REGION_xx);
			```
			다음 열거형은 API 위에서 사용할 수 있는 mpu 유형을 제공합니다.
			```c
			enum mpu_region_usages_e {
			        MPU_REGION_BOARD_SPECIFIC,
			        MPU_REGION_COMMON_BIN,
			        MPU_REGION_APP_BIN,
			        MPU_REGION_STACKOVF,
			        MPU_REGION_MAX
			};
			```
			2. 두 번째 단계에서는 mpu 레지스터 값을 가져와 아래 함수를 사용하여 배열에 저장합니다.
			```c
			/****************************************************************************
			 * Name: mpu_get_register_config_value
			 *
			 * Description:
			 *   Configure the user application SRAM mpu settings into the tcb variables
			 *
			 * Params:
			 *  regs     : pointer to array in which to store the values to be configured
			 *  region   : number of the region to be configured
			 *  base     : start address for the region
			 *  size     : size of the region in bytes
			 *  readonly : true indicates a readonly region
			 *  execute  : true indicates that the region has execute permission
			 ****************************************************************************/

			void mpu_get_register_config_value(uint32_t *regs, uint32_t region, uintptr_t base, size_t size, uint8_t readonly, uint8_t execute);
			```
			여기서,  
			두 번째 매개변수(영역) 값은 위의 1단계에서 얻은 값에서 사용됩니다. 이는 다시 로드 시간을 최적화하기 위해 3개의 개별 MPU 영역(text, ro 및 rw)을 기반으로 (nregion - 1)에서 (nregion - x)까지 확장되거나 모든 섹션 데이터에 대해 하나의 MPU 영역만 최적화합니다.  
			3. 세 번째 단계에서는 배열의 값을 사용하여 아래 함수를 사용하여 mpu를 구성합니다.
			```c
			/****************************************************************************
			 * Name: up_mpu_set_register
			 *
			 * Description:
			 *   Set MPU register values to real mpu h/w
			 *
			 ****************************************************************************/
			void up_mpu_set_register(uint32_t *mpu_regs);
			```

4. **새 MPU 지역 추가 방법**
	- 새로운 유형의 MPU 사용법을 추가하려면 공통 mpu.h 헤더 파일(os/include/tinyara/mpu.h)의 아래 열거형에서 다른 항목을 수행해야 합니다.
	```c
	enum mpu_region_usages_e {
	        MPU_REGION_BOARD_SPECIFIC,
	        MPU_REGION_COMMON_BIN,
	        MPU_REGION_APP_BIN,
	        MPU_REGION_STACKOVF,
	        MPU_REGION_MAX
	};
	```
	- MPU 영역의 순서는 아래 규칙을 고려하여 위와 같이 유지됩니다.
		- armv7-m에서 MPU 영역 간의 주소 범위가 겹치는 경우 더 높은 MPU 지역 번호의 속성이 낮은 번호의 MPU 지역에 의해 설정된 속성을 재정의합니다.
		- armv8-m에서는 영역 중복이 허용되지 않으므로 MPU 영역의 순서는 중요하지 않습니다.
	- 기존의 보드 특정, 공통 바이너리, 앱 바이너리 및 스택 오버플로의 용도 외에 새로운 MPU 영역(nregion_xx)을 위 구조에 추가해야 합니다.

5. **예: RO 설정**를 사용하여 새 MPU 영역 MPU_REGION_MYREGION 추가
	1. 공통 mpu.h 파일의 열거형 mpu_region_usages_e에 MPU 지역 유형 MPU_REGION_MYREGION를 추가합니다.
	```c
	enum mpu_region_usages_e {
	 ....
	 MPU_REGION_STACKOVF,
	 MPU_REGION_MYREGION,
	 MPU_REGION_MAX
	};
	```
	2. 공통 mpu.h 파일의 구조체 mpu_usages_s에 MPU 영역 값을 저장하려면 변수 nregion_myregion를 추가합니다.
	```c
	struct mpu_usages_s {
	 ....
	 uint8_t nregion_stackovf;
	 uint8_t nregion_myregion;
	 uint8_t max_nregion;
	};
	```
	3. mpuinit.c 파일에서 MPU 초기화 중에 MPU 영역을 해당 번호로 초기화했습니다.
	```c
	void mpu_region_initialize(struct mpu_usages_s *mpu) {
	...
	 offset += 1;
	 mpu->nregion_myregion = offset;
	}
	```
	4. MPU 지역 번호를 얻으려면 mpu_get_nregion_info(사용량) API를 사용하세요.
	```c
	uint8_t nregion = mpu_get_nregion_info(MPU_REGION_MYREGION);
	```
	5. 이 MPU 영역을 RO로 구성하려면:
		1. mpu_get_register_value 함수를 사용하여 mpu 레지스터 값을 얻습니다.
			- *regs* 배열 변수(크기 3\*(uint32_t))를 선언하고 이를 첫 번째 인수로 전달합니다.
			- 세 번째 단계에서 두 번째 인수로 얻은 Pass 지역 번호입니다.
			- 읽기 전용 인수를 *True*로 전달합니다.
			- MPU 영역 기본 주소, 크기 및 실행 권한에 대한 적절한 값을 전달합니다.
			```c
			uint8_t nregion = mpu_get_nregion_info(MPU_REGION_MYREGION);
			mpu_get_register_config_value(regs, nregion - 1,  base_address, size, True,  execute);
			```
		2. up_mpu_set_register 함수를 사용하여 얻은 mpu 레지스터 값을 mpu h/w로 설정하여 지역을 구성합니다.
		```c
		up_mpu_set_register(regs);
		```

<a id="mpu-usages-of-platform"></a>
## MPU 플랫폼 용도
- In Protected Build에서 커널은 권한 있는 모드에서 실행되고 모든 메모리 범위에 액세스할 수 있으며 MPU 영역을 사용하여 권한이 없는 사용자 코드에 대한 메모리 액세스 권한을 정의합니다.
- 아래 표에서는 다양한 TizenRT 기능에 대한 MPU 구성을 설명합니다(해당 구성으로 활성화됨).
![MPU 플랫폼 테이블의 용도](../docs/media/MPU_Usages_of_platform_table.png)

<a id="sample-memory-layout-of-mpu-regions"></a>
## MPU 영역의 샘플 메모리 레이아웃

- 아래 다이어그램은 MPU 영역에 대한 메모리 레이아웃의 예입니다.
- 이 예에서는 아래 부울 구성이 활성화되었습니다.
	1. CONFIG_APP_BINARY_SEPARATION
	2. CONFIG_OPTIMIZE_APP_RELOAD_TIME
	3. CONFIG_SUPPORT_COMMON_BINARY
	4. CONFIG_MPU_STACK_OVERFLOW_PROTECTION

```
		RAM

	+===============+
	|	 	|
	|		|
Kernel	|  Kheap/KStack |
Heap	|		|
	|===============|
	|		|
	|		|  MPU_APP_TEXT(ROX) MPU_APP_RO(RONX) MPU_APP_DATA(RWNX)    MPU_STACK(RONX)    RWNX            RONX           RWNX
	|		|     +------------------------------------------------------------------------------------------------------------------+
App1	|  Utext/Udata/	| ==> | Utext    |   Uro/Udata    |   Udata/Ubss/Uheap    | Guard Region | Task1 Ustack  | Guard Region | Task2 Ustack   |
Heap	|  Ubss/Uheap/	|     +------------------------------------------------------------------------------------------------------------------+
	|  Ustack	|
	|		|
	|===============|
	|		|
	|		|  MPU_APP_TEXT(ROX) MPU_APP_RO(RONX) MPU_APP_DATA(RWNX)    MPU_STACK(RONX)    RWNX             RONX          RWNX
	|		|     +------------------------------------------------------------------------------------------------------------------+
App2	|  Utext/Udata/	| ==> |  Utext    |   Uro/Udata    |   Udata/Ubss/Uheap    | Guard Region | Task3 Ustack  | Guard Region | Task4 Ustack  |
Heap	|  Ubss/Uheap/	|     +------------------------------------------------------------------------------------------------------------------+
	|  Ustack	|
	|		|
	|===============|
	|		|
	|		|           MPU_COM_LIB_TEXT(ROX)          MPU_COM_LIB_RO(RONX)         MPU_COM_LIB_DATA(RWNX)
	|		|     +-------------------------------------------------------------------------------------------+
Common	|  Utext/Udata/	| ==> |           Utext             |         Uro/Udata           |     Udata/Ubss/Uheap/Ustack   |
Binary	|  Ubss/Uheap/	|     +-------------------------------------------------------------------------------------------+
Heap	|  Ustack	|
	|		|
	+===============+
```

<a id="appendix"></a>
## 부록
- 현재 TizenRT에서는 ARMV7M 및 ARMV8M MPU만 지원됩니다.
	- ARMV8M MPU 지원은 구현되었지만 어떤 보드에서도 검증되지 않았습니다.
- MPU 영역을 구성하려면 ARM MPU 레지스터(아키텍처별 mpu.h 파일, 예: os/arch/arm/src/armv7-m/mpu.h에 정의됨) 아래를 설정해야 합니다.
	1. **MPU_RNR** : MPU 지역 번호 레지스터
	2. **MPU_RBAR** : MPU 지역 기본 주소 레지스터
	3. **MPU_RASR** : MPU 지역 속성 및 크기 레지스터
- ARMV7M MPU에는 다음과 같은 기능이 있습니다.
	- MPU는 0~16개 영역 사이에서 구성 가능한 프로그래밍 가능 영역 수를 지원합니다.
	- MPU 메모리 영역 기본 주소는 영역 크기의 정수배로 정렬되어야 합니다.
	- MPU 영역 크기는 2의 거듭제곱이어야 하지만 32바이트보다 작을 수 없습니다.
- ARMV8M MPU에는 다음과 같은 기능이 있습니다.
	- MPU는 보안 상태당 0~16개 영역 사이에서 구성 가능한 프로그래밍 가능 영역 수를 지원합니다.
	- MPU 영역에 대해 프로그래밍할 수 있는 가장 작은 크기는 32바이트입니다.
	- MPU 영역의 최대 크기는 4GB이지만 크기는 32바이트의 배수여야 합니다.
	- 모든 영역은 32바이트로 정렬된 주소에서 시작해야 합니다.
- ARM MPU에 대한 자세한 내용은 아래 문서를 참조하세요.
	- [메모리 보호 장치(MPU)](https://static.docs.arm.com/100699/0100/armv8m_architecture_memory_protection_unit_100699_0100_00_en.pdf)
