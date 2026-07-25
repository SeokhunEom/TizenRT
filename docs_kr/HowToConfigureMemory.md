# 메모리 구성 방법

## 목차
- [구성](#configuration)
- [링커 스크립트](#linker-scripts)
- [다중 힙 지원](#multi-heap-support)

<a id="configuration"></a>
## 구성
메모리를 구성하는 구성에는 CONFIG_RAM_REGIONx_START와 CONFIG_RAM_REGIONx_SIZE의 두 가지가 있습니다.  
Menuconfig는 아래와 같이 변경하는 데 도움이 됩니다.
1. menuconfig 옵션을 사용하여 make를 실행합니다.
	```
	cd $TIZENRT_BASEDIR
	cd os
	make menuconfig
	```
2. 구성을 찾으려면 `Hardware Configuration -> Chip selection -> Boot Memory Configuration`를 선택하세요.


3. `CONFIG_RAM_REGIONx_START`를 16진수 값으로 설정하고 `CONFIG_RAM_REGIONx_SIZE`를 바이트 단위의 10진수 값으로 설정합니다.
	```
	CONFIG_RAM_REGIONx_START=0x02023800
	CONFIG_RAM_REGIONx_SIZE=968704
	```
힙 할당은 힙 시작 및 끝 주소를 설정하는 데 이러한 값을 사용합니다.

<a id="linker-scripts"></a>
## 링커 스크립트
링커 스크립트에는 RAM 시작 및 크기 정보도 있습니다.  
파일 경로 : *build/configs/<BOARD_NAME>/scripts/<SCRIPTS_NAME>.ld*  

```
MEMORY
{
...
	SRAM	(rwx)	: ORIGIN = 0x02023800, LENGTH = 946K
...
}
```
이 값은 TizenRT 구성 파일과 일치해야 합니다.

<a id="multi-heap-support"></a>
## 다중 힙 지원
TizenRT는 H/W에 이전 파티션과 불연속적인 주소를 가진 또 다른 RAM 파티션이 있는 경우 다중 힙을 지원할 수 있습니다.  
[다중 메모리 맵을 힙으로 사용하는 방법](HowToUseMultiHeap.md)를 참조하세요.


