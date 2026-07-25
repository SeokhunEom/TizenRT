# TizenRT의 로드 가능한 애플리케이션 구성에 대한 사용자 가이드

## 목차

- [로드 가능한 앱 개요](#overview-of-loadable-apps)
- [가정 및 제한 사항](#assumptions-and-limitations)
- [기존 로드 가능한 앱 구성을 활성화하는 단계](#steps-to-enable-existing-loadable-apps-configuration)
- [새로운 로드 가능한 앱 구성을 추가하는 단계](#steps-to-add-new-loadable-apps-configuration)

<a id="overview-of-loadable-apps"></a>
## 로드 가능한 앱 개요
- Loadable Apps의 경우 별도의 커널 바이너리 외에도 각 사용자 공간 애플리케이션마다 별도의 바이너리가 생성됩니다. 이러한 바이너리는 플래시에 저장되고 런타임 중에 RAM에 로드됩니다.
- Binary 관리자는 바이너리를 관리하고 시스템의 오류를 복구하는 커널 스레드입니다. 오류 관리자는 복구 스레드로 바이너리 관리자에 통합됩니다. 결함이 있는 앱을 식별하고 필요한 조치를 취합니다.
- 바이너리 관리자의 우선 순위는 203이고 바이너리 관리자 로더는 200인 반면 hpwork 스레드의 우선 순위는 201입니다. 이는 바이너리 관리자 로더 중에 hpwork가 실행되어야 함을 보장하기 위한 것입니다.
- 이는 바이너리를 로드하는 동안에도 실시간 스레드의 우선 순위를 보장합니다. 우선순위는 다음과 같습니다.

```
	----------------------------------------
	MODULE				PRIORITY
	----------------------------------------
	BINARY_MANAGER_PRIORITY		  203
	CONFIG_SCHED_HPWORKPRIORITY	  201
	LOADINGTHD_PRIORITY		  200
	CONFIG_SCHED_LPWORKPRIORITY	  50
	----------------------------------------
```
- 시스템의 더 나은 오류 관리를 위해서는 로드 가능한 앱이 필요합니다. 시스템에서 메모리 오류가 감지된 경우 전체 시스템을 재부팅할 필요 없이 애플리케이션을 다시 로드할 수 있습니다(중요도 및 구성에 따라 다름).
- 각 애플리케이션마다 MPU 기반 메모리 격리 및 보호 기능이 마련되어 있습니다. 이러한 앱은 오류 감지 후에도 메모리 공간에 다시 로드될 수 있으므로 로드 가능이라고 합니다.
- TizenRT는 TizenRT 3.0에서 micom 및 wifi라는 2개의 샘플 로드 가능한 앱을 제공합니다.
- 이 외에도 사용자는 요구 사항에 따라 로드 가능한 새 앱을 추가할 수도 있습니다.

<a id="assumptions-and-limitations"></a>
## 가정 및 제한

1. Protected 빌드가 구성에서 활성화되었습니다(CONFIG_BUILD_PROTECTED=y).
[보호된 빌드 가이드](ProtectedBuildGuide.md) 문서에서는 새 칩셋에 대해 TizenRT 보호 빌드를 포팅하는 방법에 대한 지침을 제공합니다.
2. 애플리케이션을 수용하려면 플래시 및 램 메모리를 사용할 수 있어야 합니다.
3. 애플리케이션의 총 RAM 요구 사항은 빌드 시 수정되어야 합니다.
4. 현재 TizenRT는 elf 바이너리 형식만 지원합니다. 드라이버와 같은 커널 공간의
5. Code는 아래와 같이 사용자 공간의 메모리에 액세스해야 하는 일부 API를 사용할 수 없습니다.

 사용자 API | 커널 API
--------------------------------|------------------------------------------
 malloc() | kmm_malloc()
 calloc() | kmm_calloc()
 zalloc() | kmm_zalloc()
 realloc() | kmm_realloc()
 memalign() | kmm_memalign()
 무료() | kmm_free()
 pthread_create() | kernel_thread()
 task_create() | kernel_thread()

<a id="steps-to-enable-existing-loadable-apps-configuration"></a>
## 기존 로드 가능한 앱 구성을 활성화하는 단계

<a id="configuration-settings"></a>
### 구성 설정

#### E필수 구성

- 다음 구성을 활성화해야 합니다.

```
 ------------------------------------------------------------------------------------------------------
 CONFIG FLAG					VALUE		DESCRIPTION
 ------------------------------------------------------------------------------------------------------

 CONFIG_APP_BINARY_SEPARATION			y		App binary separation

 CONFIG_NUM_APPS				x		x = number of apps

 CONFIG_EXAMPLES_ELF				y		Loadable Apps test

 CONFIG_BINARY_MANAGER				y		Binary Manager
 CONFIG_BINMGR_RECOVERY				y		Binary Recovery Management (OPTIONAL)
 CONFIG_BINMGR_UPDATE				y		Binary Manager Update APIs (OPTIONAL)

 CONFIG_BINFMT_ENABLE				y		Binary Loader
 CONFIG_BINFMT_LOADABLE				y		Loadable binary format
 CONFIG_ELF					y		Enable the ELF Binary Format
 CONFIG_ELF_ALIGN_LOG2				2		Log2 Section Alignment
 CONFIG_ELF_STACKSIZE				2048		ELF Stack Size
 CONFIG_ELF_BUFFERSIZE				32		ELF I/O Buffer Size
 CONFIG_ELF_BUFFERINCR				32		ELF I/O Buffer Realloc Increment
 CONFIG_ELF_EXCLUDE_SYMBOLS			y		Excludes symbol information from ELF file
 CONFIG_ELF_CACHE_READ				n		ELF cache read support
 CONFIG_SYMTAB_ORDEREDBYNAME			n		Symbol Tables Ordered by Name
 CONFIG_OPTIMIZE_APP_RELOAD_TIME		y		Optimizations for application reload time
 ------------------------------------------------------------------------------------------------------

```

#### 옵션 구성

이는 로드 가능한 핵심 앱의 일부는 아니지만 일부 기능을 테스트하기 위해 선택적으로 활성화할 수 있습니다.
활성화된 추가 테스트를 수용할 수 있도록 최대 작업 크기를 늘려야 합니다.

1. MPU가 활성화된 경우 아래와 같이 MPU 테스트도 활성화할 수 있습니다.

```
CONFIG_EXAMPLES_MEM_PROTECT_TEST=y
CONFIG_MEM_PROTECTTEST_KERNEL_CODE_ADDR=0x60000000
CONFIG_MEM_PROTECTTEST_KERNEL_DATA_ADDR=0x20200000
CONFIG_MEM_PROTECTTEST_APP_ADDR=0x80200100
```
   위 시나리오에서 구성 설명은 다음과 같습니다.

```
CONFIG_MEM_PROTECTTEST_KERNEL_CODE_ADDR		This is same as FLASH address		CONFIG_FLASH_START_ADDR
CONFIG_MEM_PROTECTTEST_KERNEL_DATA_ADDR		This is same as SRAM address		CONFIG_SRAM_START_ADDR
CONFIG_MEM_PROTECTTEST_APP_ADDR			Application sections' address		CONFIG_RAM_REGIONx_START
					(described below)
```

2. 활성화할 수 있는 기타 로드 가능한 앱 테스트 구성은 다음과 같습니다.

```
#
# Enable Test Scenarios
#
CONFIG_EXAMPLES_MESSAGING_TEST=y
CONFIG_MESSAGING_TEST_REPETITION_NUM=1
CONFIG_EXAMPLES_RECOVERY_TEST=y
# CONFIG_ENABLE_RECOVERY_AGING_TEST is not set
# CONFIG_EXAMPLES_BINARY_UPDATE_TEST is not set
# CONFIG_EXAMPLES_MICOM_TIMER_TEST is not set
CONFIG_EXAMPLES_ELF_FULLYLINKED=y
```

<a id="memory-settings"></a>
### 메모리 설정

ELF는 플래시 파티션에 저장되고 런타임 시 RAM에 로드되므로 예약해야 합니다.
RAM 및 FLASH 메모리 모두.

#### Flash 파티션 설정

- 아래 제공된 3가지 구성을 사용하여 각 애플리케이션에 대해 새 플래시 파티션을 추가합니다.
	1. '크기'는 바이너리 크기(CONFIG_FLASH_PART_SIZE)를 수용할 수 있을 만큼 커야 합니다.
	2. 'type'은 "bin" 유형(CONFIG_FLASH_PART_TYPE)이어야 합니다.
	3. 'name'은 애플리케이션의 이름(CONFIG_FLASH_PART_NAME)일 수 있습니다.

  아래 시나리오에서는 각각 1MB 크기의 micom 및 wifi라는 앱이 추가됩니다. 두 인스턴스(wifi,wifi)는 바이너리의 FOTA 업데이트(지원되는 경우)를 의미합니다. 하나는 실제 바이너리를 포함하고 다른 하나는 업데이트된 바이너리를 포함합니다.

```
CONFIG_FLASH_PART_SIZE="2048,512,1024,1024,1024,1024,1024,128,128,"
CONFIG_FLASH_PART_TYPE="kernel,none,bin,bin,bin,bin,smartfs,ftl,config,"
CONFIG_FLASH_PART_NAME="kernel,app,micom,micom,wifi,wifi,userfs,ftl,config,"
```

#### RAM 메모리 설정

- 로드 가능한 앱의 텍스트 및 데이터 섹션은 사용자 RAM 영역에서 할당됩니다.
- 그래서 애플리케이션을 로드할 수 있을 만큼 큰 메모리 영역을 찾으세요.
- CONFIG_RAM_REGIONx_START 및 CONFIG_RAM_REGIONx_SIZE에 주소 및 크기를 추가합니다(아직 없는 경우).

  아래 시나리오에서는 0x80200000에서 시작하는 30MB 크기의 영역을 추가했습니다.

```
CONFIG_RAM_REGIONx_START="0x20200000,0x80200000"
CONFIG_RAM_REGIONx_SIZE="262144,31457280"
```

<a id="steps-to-add-new-loadable-apps-configuration"></a>
## 새 로드 가능한 앱 구성을 추가하는 단계

<a id="configuration-settings"></a>
### 구성 설정

- 여기에 명시된 요구 사항([구성 설정](#configuration-settings))에 따라 로드 가능한 앱과 관련된 모든 구성을 활성화하세요.

<a id="memory-settings"></a>
### 메모리 설정

- 새 애플리케이션 추가를 위해 플래시 및 램 파티션을 예약하려면 여기에 명시된 요구 사항([메모리 설정](#memory-settings))을 따르세요.

### 코드 변경 필요

- "loadable_apps/loadable_sample/<app_name>"에 필수 앱을 추가하세요.

#### Makefile

- 새로 추가된 각 애플리케이션에 대해 elf 유형 바이너리를 생성해야 합니다.
  <os/loadable_apps/loadable_sample/app/Makefile>에서는,

```
ifeq ($(CONFIG_APP_BINARY_SEPARATION),y)

BIN = wifi
BIN_TYPE = ELF
BIN_VER = 20190412
DYNAMIC_RAM_SIZE = 512000
KERNEL_VER = 2.0
STACKSIZE = 8192
PRIORITY = 180

include $(TOPDIR)/$(LOADABLEDIR)/loadable.mk
endif # CONFIG_APP_BINARY_SEPARATION

```
여기서,
- BIN_TYPE: 바이너리 유형(elf여야 함).
- STACKSIZE: 첫 번째 작업에는 이 크기의 스택이 포함됩니다.
- PRIORITY: 앱의 첫 번째(주) 작업이 이 우선순위로 생성됩니다.
- DYNAMIC_RAM_SIZE: 앱에서 사용할 수 있는 총 RAM 양입니다. 개발자는 앱의 최대 RAM 요구 사항을 평가해야 합니다. 런타임 중에 메모리 요구 사항이 이 값을 초과하면 앱이 실패합니다. 이 경우 사용자는 이에 대해 더 높은 값을 제공하고 앱을 다시 빌드한 후 다시 실행해야 할 수 있습니다.

이 값은 바이너리 관리자가 애플리케이션의 첫 번째 프로세스를 시작하는 데 사용됩니다.

#### Make.defs

- 다음과 같이 여기에 앱 폴더를 포함합니다.

```
CONFIGURED += loadable_sample/<app>
```

#### 애플리케이션 코드

- 아래는 wifiapp이 중요하지 않은 앱인 로드 가능한 앱의 주요 서명입니다.
  는 Tash를 로드하기 위해 preapp_start()를 호출합니다.

```
extern int preapp_start(int argc, char **argv);

#ifdef CONFIG_APP_BINARY_SEPARATION
int main(int argc, char **argv)
#else
int wifiapp_main(int argc, char **argv)
#endif
{
.
.
.
#if defined(CONFIG_SYSTEM_PREAPP_INIT) && defined(CONFIG_APP_BINARY_SEPARATION)
        preapp_start(argc, argv);
#endif
```

- 추가된 앱의 주요 기능에 바이너리 관리자 알림을 추가합니다.

```
#ifdef CONFIG_BINARY_MANAGER
        ret = binary_manager_notify_binary_started();
        if (ret < 0) {
                printf("MICOM notify 'START' state FAIL\n");
        }
#endif

```
