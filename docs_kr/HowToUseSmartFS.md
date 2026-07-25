# SmartFS 활성화 및 사용 방법
SmartFS는 TizenRT의 사용자 파티션에 사용되는 파일 시스템입니다. 이 문서에서는 TizenRT의 SmartFS, 특히 해당 아키텍처에 대한 세부 정보와 새 대상 보드에서 이를 활성화하는 방법을 제공합니다.

## 목차
- [SmartFS 소개](#about-smartfs)  
- [SmartFS 코드 계층화](#smartfs-code-layering)  
- [새 보드에서 SmartFS를 활성화하는 방법](#how-to-enable-smartfs-on-new-board)  
- [SmartFS 사용자 이미지를 만드는 방법](#how-to-make-smartfs-user-image)  

<a id="about-smartfs"></a>
## SmartFS 정보

SmartFS는 SMART(SMART) 플래시를 위한 섹터 매핑 할당을 나타냅니다.  
소규모의 기본 작업을 위해 설계된 파일 시스템입니다.
1M byte ~ 16M byte 크기의 직렬 NOR형 플래시 부품
(이것은 제한 사항이 아니지만).

파일 시스템은 플래시(또는 플래시 파티션)를 분할하여 작동합니다.
동일한 크기의 "논리적 섹터"로 나누어 관리(할당,
매핑, 연결, 해제 등)을 수행하여 파일과 디렉터리를 구축합니다.

이 가이드에서는 새 보드에서 SmartFS를 활성화하는 단계와 세부 정보를 설명합니다.

<a id="smartfs-code-layering"></a>
## SmartFS 코드 계층화

SmartFS는 표준 MTD 드라이버 위에 구축된 두 개의 레이어로 구성됩니다.  
아래 다이어그램은 두 개의 SmartFS 레이어에 관한 코드 레이어를 보여줍니다.

```
+----------------------------------------------+
| Smart FS (Smart File System Layer)           |
+----------------------------------------------+
| Smart MTD Driver (Smart Block Driver Layer)  |
+----------------------------------------------+
| MTD Driver  (MTD Block Driver Layer)         |
+----------------------------------------------+
| FLASH Driver (Flash Driver)                  |
+----------------------------------------------+
```

MTD 드라이버 바로 위의 코드는 SMART MTD 레이어입니다.  
이는 MTD(플래시 드라이버) 레이어와 인터페이스하고 낮은 수준을 처리합니다.
논리 섹터 할당, 해제 및 관리와 같은 미디어 작업,
블록 관리 지우기, 로우 레벨 포맷, 웨어 레벨링 등

SMART MTD 계층 위에는 스마트 파일 시스템 계층이 있습니다.  
SmartFS 코드는 SMART MTD 계층의 논리 섹터 서비스를 사용하여
새 파일 생성과 같은 파일 및 디렉터리 수준 관리 제공
논리 섹터를 함께 연결하여 파일 생성 및 디렉터리 생성/files
관리 루틴으로.

<a id="how-to-enable-smartfs-on-new-board"></a>
## 새 보드에서 SmartFS를 활성화하는 방법
1. SoC/보드에 있는 플래시용 *[FLASH 드라이버](#flash-driver)*를 추가합니다.  
2. 플래시에 해당하는 *[MTD 드라이버](#mtd-driver)*를 활성화합니다.  
3. *[SMART MTD 드라이버](#smart-mtd-driver)*를 활성화합니다.  
4. *[SMART FS](#smart-fs)*를 활성화합니다.

<a id="flash-driver"></a>
### FLASH 드라이버
플래시 드라이버는 FLASH HW에 대해 낮은 수준의 read/write/erase 기능을 제공해야 합니다.  
플래시 드라이버의 예가 *os/arch/arm/src/s5j/s5j_sflash.c*에 있습니다.

부팅하는 동안 플래시 드라이버는 다음 예와 유사하게 초기화되어야 합니다.
```c
/* In case of s5j */
void arm_boot(void)
{
	...
	s5j_board_initialize();
	...
}

void s5j_board_initialize(void)
{
#ifdef CONFIG_S5J_SFLASH
        s5j_sflash_init();
#endif
}
```
```c
/* In case of imxrt */
void __start(void)
{
	...
	/* Initialize onboard resources */
	imxrt_boardinitialize();
	...
}

void imxrt_boardinitialize(void)
{
	...
	imxrt_flash_init();
}
```
<a id="mtd-driver"></a>
### MTD 드라이버 기본 FLASH 드라이버에 액세스하려면
MTD 드라이버를 활성화해야 합니다.  
그러나 MTD 드라이버 초기화는 대상 보드에 따라 다릅니다.

MTD 드라이버를 사용하려면 아래 구성을 활성화해야 합니다.
1. CONFIG_MTD 활성화  
	```
	File Systems -> Memory Technology Device (MTD) Support = y
	```
2. CONFIG_MTD_PARTITION 활성화  
	```
	File Systems -> Memory Technology Device (MTD) Support -> Support MTD partitions = y
	```
3. CONFIG_MTD_PARTITION_NAMES 활성화  
	```
	File Systems -> Memory Technology Device (MTD) Support -> Support MTD partition  naming = y
	```

MTD 드라이버 초기화의 예는 *os/board/common/partitions.c*를 참조하세요.
```c
#ifdef CONFIG_MTD_PROGMEM
	mtd = progmem_initialize();
	if (!mtd) {
		lldbg("ERROR: progmem_initialize failed\n");
		return;
	}

	if (mtd->ioctl(mtd, MTDIOC_GEOMETRY, (unsigned long)&geo) < 0) {
		lldbg("ERROR: mtd->ioctl failed\n");
		return;
	}
#else
	mtd = up_flashinitialize();
	if (!mtd) {
		lldbg("ERROR : up_flashinitializ failed\n");
		return;
	}

	if (mtd->ioctl(mtd, MTDIOC_GEOMETRY, (unsigned long)&geo) < 0) {
		lldbg("ERROR: mtd->ioctl failed\n");
		return;
	}
#endif
```

프로그램 작동을 지원하는 s5j와 같은 보드의 경우(*os/fs/driver/mtd/mtd_progmem.c* 참조),  
CONFIG_MTD_PROGMEM를 먼저 활성화해야 합니다.  
```
File Systems -> Memory Technology Device (MTD) Support -> Enable on-chip program FLASH MTD device = y
```

imxrt와 같은 보드의 경우 CONFIG_MTD_PROGMEM가 비활성화되어 up_flashinitialize를 구현해야 합니다.
(*os/arch/arm/src/imxrt/imxrt_norflash.c* 참조)


<a id="smart-mtd-driver"></a>
### SMART MTD 드라이버
스마트 MTD 레이어는 CONFIG_MTD_SMART 구성 매개변수를 통해 활성화할 수 있습니다.
```
File Systems -> Memory Technology Device (MTD) Support -> Sector Mapped Allocation for Really Tiny (SMART) Flash support = y
```

스마트 MTD 드라이버 코드는 *os/fs/driver/mtd/smart.c*를 참조하세요.

그리고 아래와 같이 스마트 기기를 초기화할 수 있습니다. (*os/board/common/partitions.c* 참조)
```c
#ifdef CONFIG_MTD_CONFIG
		if (!strncmp(types, "config,", 7)) {
			mtdconfig_register(mtd_part);
		} else
#endif
#if defined(CONFIG_MTD_SMART) && defined(CONFIG_FS_SMARTFS)
		if (!strncmp(types, "smartfs,", 8)) {
			char partref[4];

			snprintf(partref, sizeof(partref), "p%d", partno);
			smart_initialize(CONFIG_FLASH_MINOR, mtd_part, partref);
		} else
#endif
		{
		}

```
위의 코드 조각은 */dev/smart0p[partno]*와 유사한 스마트 장치 노드를 생성합니다.  


<a id="smart-fs"></a>
### SMART FS
Smart FS 레이어는 아래 구성을 활성화하여 활성화할 수 있습니다.  
1. CONFIG_FS_SMARTFS 활성화
	```
	File Systems -> SMART file system = y
	```
2. CONFIG_SMARTFS_ERASEDSTATE 설정
	```
	File Systems -> SMART file system -> SMARTFS options -> FLASH erased state = 0xff
	```
3. Set CONFIG_SMARTFS_MAXNAMLEN=32
	```
	File Systems -> SMART file system -> SMARTFS options -> Maximum file name length = 32
	```
4. CONFIG_SMARTFS_ALIGNED_ACCESS 활성화
	```
	File Systems -> SMART file system -> SMARTFS options -> Ensure 16 and 32 bit accesses are aligned = y
	```

다음은 선택적 기능 구성입니다. 자세한 내용은 *os/fs/smartfs/Kconfig*를 참조하세요.  
1. CONFIG_SMARTFS_MULTI_ROOT_DIRS
	```
	File Systems -> SMART file system -> SMARTFS options -> Support multiple Root Directories/Mount points
	```
2. CONFIG_SMARTFS_DYNAMIC_HEADER
	```
	File Systems -> SMART file system -> SMARTFS options -> Dynamic Header
	```
3. CONFIG_SMARTFS_JOURNALING
	```
	File Systems -> SMART file system -> SMARTFS options -> Enable filesystem journaling for smartfs
	```
4. CONFIG_SMARTFS_SECTOR_RECOVERY
	```
	File Systems -> SMART file system -> SMARTFS options -> Enable recovery of lost sectors in Filesystem
	```
SmartFS 파일 시스템은 스마트 mtd 장치에 마운트될 수 있습니다.  
장착하기 전에 스마트 장치를 mksmartfs 기능을 사용하여 smartfs로 포맷해야 합니다.  
SmartFS 포맷 및 마운트를 위한 샘플 코드:
```
#ifdef CONFIG_ARTIK05X_AUTOMOUNT_USERFS
	/* Initialize and mount user partition (if we have) */
	ret = mksmartfs(ARTIK05X_AUTOMOUNT_USERFS_DEVNAME, false);
	if (ret != OK) {
		lldbg("ERROR: mksmartfs on %s failed\n",
				ARTIK05X_AUTOMOUNT_USERFS_DEVNAME);
	} else {
		ret = mount(ARTIK05X_AUTOMOUNT_USERFS_DEVNAME,
				CONFIG_ARTIK05X_AUTOMOUNT_USERFS_MOUNTPOINT,
				"smartfs", 0, NULL);
		if (ret != OK) {
			lldbg("ERROR: mounting '%s' failed\n",
					ARTIK05X_AUTOMOUNT_USERFS_DEVNAME);
		}
	}
#endif /* CONFIG_ARTIK05X_AUTOMOUNT_USERFS */
```
Smart FS는 아래와 같이 TASH Shell에도 장착할 수 있습니다.  
1. mksmartfs 명령을 사용하여 파티션 포맷
	```
	Usage   : mksmartfs <device name>
	Example : mksmartfs /dev/smart0p8
	```
2. 마운트 명령을 사용하여 smartfs 마운트
	```
	Usage   : mount -t <fs-type> <source/device name> <target/ logical mount path>
	Example : mount -t smartfs /dev/smart0p8 /mnt
	```

<a id="how-to-make-smartfs-user-image"></a>
## SmartFS 사용자 이미지 만드는 방법

TizenRT는 사용자 콘텐츠로 SmartFS 사용자 이미지를 만드는 방법을 제공합니다. 파일 시스템 파티션에 프로그래밍하는 것이 가능합니다.  
자세한 내용은 [SmartFS 사용자 이미지를 만드는 방법](../tools/nxfuse/README_SMARTFS.md)에서 확인하세요.
