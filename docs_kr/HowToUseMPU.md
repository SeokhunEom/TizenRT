## MPU
구성에 따라 12개 또는 16개의 메모리 영역을 지원합니다. ARMv7-R에 메모리 영역이 12개 있다는 점을 고려하여 구성이 선택되었습니다.
각 영역은 기본 주소와 크기로 프로그래밍되어야 합니다.

## 목차
- [MPU 초기화 및 메모리 영역 설정](#mpu-initialization-and-memory-region-setup)
- [메모리 영역 설정 기능](#memory-region-setup-functions)
- [예](#example)

<a id="mpu-initialization-and-memory-region-setup"></a>
## MPU 초기화 및 메모리 영역 설정
MPU의 초기화는 메모리 영역을 설정하여 수행됩니다. 메모리 영역은 아래 표의 함수를 사용하여 설정되며 각 함수에는 메모리 영역을 설정하기 위해 _Region Base Address_ 및 _Region Size_라는 두 개의 인수가 필요합니다.

이름이 ```_priv_```인 함수는 _kernel_(권한 있음) MPU 영역을 구성하는 데 사용되는 반면, ```_user_``` 함수는 _user_(권한 없음) MPU 영역을 구성하는 데 호출됩니다.

MPU 영역 설정 관련 매크로 및 기능은 *os/arch/arm/src/armv7-r/* 디렉터리 아래 **[mpu.h](../os/arch/arm/src/armv7-r/mpu.h)**에 정의되어 있습니다.

```
-----------------------------------
       MPU Memory Region Info
-----------------------------------
RNUM:	int32 - Region Number
RBASE:	int32 - Region Base Address
RSIZE:	int32 - Region Size
RATTR:	int32 - Region Attribute
-----------------------------------
```

```
------------------------------------------------------------------
            MPU Init or Memory Region Setup Functions
------------------------------------------------------------------
mpu_control() : This function is used to Enable or Disable the MPU.
------------------------------------------------------------------
                       Memory Region Access Permission Attributes
                             Privileged      Un-Privileged
------------------------------------------------------------------
mpu_priv_stronglyordered() : Read Write        No Access
mpu_priv_noncache()        : Read Only         No Access
mpu_peripheral()           : Read Write        No Access
mpu_priv_flash()           : Read Only         No Access
mpu_user_flash()           : Read Only         Read Only
mpu_priv_intsram()         : Read Write        No Access
mpu_user_intsram()         : Read Write        Read Write
mpu_priv_extsram()         : Read Write        No Access
mpu_user_extsram()         : Read Write        Read Write
mpu_priv_intsram_wb()      : Read Write        No Access
mpu_user_intsram_wb()      : Read Write        Read Write
------------------------------------------------------------------
```

<a id="memory-region-setup-functions"></a>
## 메모리 영역 설정 기능

```mpu_priv_stronglyordered()``` - 이 함수는 메모리 영역을 _캐시 불가능_, _버퍼 불가능_ 및 _공유 가능_ 속성을 사용하여 강력하게 정렬된 메모리로 구성하는 데 사용됩니다.

```mpu_priv_noncache()``` - 이 함수는 _캐시 불가능_, _버퍼 불가능_, _공유 가능_ 및 _명령 액세스 비활성화_ 속성을 사용하여 메모리 영역을 내부 메모리로 구성하는 데 사용됩니다.

```mpu_peripheral()``` - 이 함수는 _shareable_, _bufferable_ 및 _instruction access disable_ 속성을 사용하여 메모리 영역을 주변 주소 공간으로 구성하는 데 사용됩니다.

```mpu_priv_flash()``` - 이 함수는 메모리 영역을 _cacheable_ 속성이 있는 권한 있는 프로그램 플래시로 구성하는 데 사용됩니다.

```mpu_user_flash()``` - 이 기능은 _cacheable_ 속성을 사용하여 메모리 영역을 사용자 프로그램 플래시로 구성하는 데 사용됩니다.

```mpu_priv_intsram()``` - 이 함수는 메모리 영역을 _shareable_, _cacheable_ 속성이 있는 권한 있는 내부 SRAM 영역으로 구성하는 데 사용됩니다.

```mpu_user_intsram()``` - 이 함수는 메모리 영역을 _shareable_, _cacheable_ 속성을 가진 사용자 내부 SRAM으로 구성하는 데 사용됩니다.

```mpu_priv_extsram()``` - 이 함수는 메모리 영역을 _shareable_, _cacheable_ 및 _bufferable_ 속성이 있는 특권 외부 SRAM 영역으로 구성하는 데 사용됩니다.

```mpu_user_extsram()``` - 이 함수는 _shareable_, _cacheable_ 및 _bufferable_ 속성을 사용하여 메모리 영역을 사용자 외부 SRAM 영역으로 구성하는 데 사용됩니다.

```mpu_priv_intsram_wb()``` - 이 함수는 mpu_priv_intsram()와 유사하지만 WB/WA 캐시 정책을 사용합니다. 

```mpu_user_intsram_wb()``` - 이 함수는 mpu_user_intsram()와 유사하지만 WB/WA 캐시 정책을 사용합니다. 

<a id="example"></a>
## 예
```
#ifdef CONFIG_ARMV7M_MPU
int s5j_mpu_initialize(void)
{
#ifdef CONFIG_ARCH_CHIP_S5JT200
	/*
	 * Vector Table		0x02020000	0x02020FFF	4
	 * Reserved		0x02021000	0x020217FF	2
	 * BL1			0x02021800	0x020237FF	8
	 * TizenRT		0x02023800	0x0210FFFF	946(WBWA)
	 * WIFI			0x02110000	0x0215FFFF	320(NCNB)
	 */

	/* Region 0, Set read only for memory area */
	mpu_priv_flash(0x0, 0x80000000);

	/* Region 1, for ISRAM(0x02020000++1280KB, RW-WBWA */
	mpu_user_intsram_wb(S5J_IRAM_PADDR, S5J_IRAM_SIZE);

	/* Region 2, wifi driver needs non-$(0x02110000++320KB, RW-NCNB */
	mpu_priv_noncache(S5J_IRAM_PADDR + ((4 + 2 + 8 + 946) * 1024), (320 * 1024));

	/* Region 3, for FLASH area, default to set WBWA */
	mpu_user_intsram_wb(S5J_FLASH_PADDR, S5J_FLASH_SIZE);

	/* region 4, for Sflash Mirror area to be read only */
	mpu_priv_flash(S5J_FLASH_MIRROR_PADDR, S5J_FLASH_MIRROR_SIZE);

	/* Region 5, for SFR area read/write, strongly-ordered */
	mpu_priv_stronglyordered(S5J_PERIPHERAL_PADDR, S5J_PERIPHERAL_SIZE);

	/*
	 * Region 6, for vector table,
	 * set the entire high vector region as read-only.
	 */
	mpu_priv_flash(S5J_IRAM_MIRROR_PADDR, S5J_IRAM_MIRROR_SIZE);

	mpu_control(true);
#endif
	return 0;
}
#endif
```
