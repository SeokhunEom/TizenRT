# 로깅 시스템 사용 방법
**디버그 메시지**는 이벤트 로깅을 위한 인터페이스이고 **로깅 모듈**는 로그 메시지, LogM 및 Syslog 장치를 처리하는 모듈입니다.

## 목차
- [디버그 메시지](#debug-messages)  
- [로깅 모듈](#logging-modules)

<a id="debug-messages"></a>
## 디버그 메시지
TizenRT는 로깅을 위해 디버그 메시지를 사용합니다.  
이러한 메시지는 [로깅 모듈](#logging-modules)를 통해 처리됩니다.  
메시지 인쇄 측면에서는 printf와 유사합니다. 그러나 menuconfig로 디버그 메시지를 구성할 수 있다는 점에서 printf와 다릅니다.  
디버그 메시지는 각 모듈 또는 레벨에 대해 구성할 수 있습니다. 대조적으로, 매크로 정의에 의해 명시적으로 비활성화하지 않는 한, printf 메시지는 빌드 시간 동안 선택적으로 비활성화될 수 없습니다.

### 새 디버그 메시지 추가
TizenRT는 디버그 수준 및 인터페이스 수준에 따라 디버그 메시지에 대한 디버그 매크로를 정의합니다.  
* Debug 수준은 로그 메시지의 심각도를 나타내며 menuconfig에서 구성으로 사용할 수 있습니다.  
   * 세 가지 디버그 수준: 오류, 경고, 정보.  
* 저수준 인터페이스는 up_putc()와 같이 장치에 직접 무언가를 쓰는 아키텍처별 I/O를 사용합니다.  
  내부적으로 인터럽트를 비활성화하여 파일 디스크립터를 사용하는 것이 부적합한 경우에만 하위 수준 코드에서 사용해야 합니다.  

이제 고려해야 할 디버그 수준과 인터페이스 수준의 6가지 조합이 있으므로 다음과 같은 6개의 매크로가 있습니다.

| 레벨 | 일반 인터페이스 | 로우 레벨 인터페이스 | 구성 |
|--------------|------------------|----------------------|----------------------|
| 오류 | dbg | lldbg | CONFIG_DEBUG_ERROR |
| 경고 | wdbg | llwdbg | CONFIG_DEBUG_WARN |
| 정보(세부 정보)| vdbg | llvdbg | CONFIG_DEBUG_VERBOSE |

TizenRT의 모듈은 적절한 기능을 정의하여 위의 디버그 매크로를 사용자 정의할 수 있습니다.  
예를 들어, 파일 시스템은 오류 출력을 위해 fdbg를 정의하고, 네트워크는 경고를 위해 nwdbg를 정의합니다.  
[os/include/debug.h](../os/include/debug.h)의 정의를 참조하세요.

### 디버그 출력을 활성화하는 방법
TizenRT에서 디버그 출력을 활성화하려면 menuconfig의 다음 단계가 필요합니다.
```
cd $TIZENRT_BASEDIR
cd os
make menuconfig
```
1. 디버깅 기능을 활성화합니다.
	```
	Debug Option -> Enable Debug Features to y
	```
2. 디버깅 수준을 활성화합니다.
	```
	Enable Error Debug Output
	Enable Warning Debug Output
	Enable Informational(Verbose) Debug Output
	```
	선택한 레벨에 따라 해당 옵션이 menuconfig에 표시됩니다.

3. 레벨이 있는 디버깅 모듈을 활성화합니다.
	예를 들어 오류 수준의 파일 시스템 디버그와 오류 및 경고 수준의 네트워크 디버그를 활성화하려면 다음을 수행합니다.
	```
	 1. Select levels, `Enable Error Debug Output` and `Enable Warning Debug output`
	 2. Select sub-level for each modules, `File System Debug Output`, `File System Error Output`,
			   `Network Debug Output`, `Network Error Output` and `Network Warning Output`
	```

<a id="logging-modules"></a>
## 로깅 모듈
기본적으로 모든 로그 출력은 stdout, 즉 콘솔로 이동합니다.  
그러나 Menuconfig를 사용하면 다른 인터페이스나 장치에도 라우팅하도록 구성할 수 있습니다.  

로깅 모듈에는 LogM과 두 개의 Syslog 장치 등 세 가지 종류가 있습니다.
 * [로그M](../os/logm/README.md)
 * [Syslog 장치](../os/drivers/syslog/README.txt) : 캐릭터 디바이스, RAM 로깅 디바이스  

LogM은 syslog보다 우선순위가 높으므로 LogM과 Syslog 장치 중 하나가 동시에 활성화되면 모든 로그가 LogM으로 전달됩니다.

### LogM 활성화 방법
LogM에 관한 자세한 내용은 [읽어보기](../os/logm/README.md)를 참조하세요.

### Syslog 장치를 활성화하는 방법
Menuconfig를 사용하면 Syslog 장치를 아래와 같이 활성화할 수 있습니다.
```
cd $TIZENRT_BASEDIR
cd os
make menuconfig
```
1. 아래 그림과 같이 Syslog 장치를 활성화합니다.
	```
	File Systems -> Advanced SYSLOG features to y
	```
2. 특정 장치를 활성화합니다.
 * 문자 장치: 특정 문자 장치에 로깅하는 데 유용합니다.
	  ```
	  System log character device to y
	  Set CONFIG_SYSLOG_DEVPATH as full path of device, CONFIG_SYSLOG_DEVPATH=/dev/ttyS1
	  ```
 * RAM 로깅 장치: 일반 직렬 출력을 사용할 수 없는 경우 유용합니다.
	  ```
	  Exit File System menu
	  Device Driver -> RAM log device support to y
	  ```
