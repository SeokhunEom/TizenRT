# 작업 모니터 사용 방법

작업 모니터는 작업을 모니터링하는 커널 스레드입니다/pthreads.  
등록된 태스크/pthreads가 살아 있는지 확인합니다.  

task/pthread가 등록된 간격 내에 활성 상태를 업데이트하지 않으면 작업 모니터가 보드를 재설정합니다.

## 목차
- [작업 모니터 활성화](#enable-task-monitor)
- [작업 모니터 사용](#use-task-monitor)


<a id="enable-task-monitor"></a>
## 작업 모니터 활성화
아래와 같이 작업 모니터를 활성화하는 4단계를 찾으십시오.

### 1. 구성 작업 모니터 활성화

menuconfig에서 **CONFIG_TASK_MONITOR**를 활성화합니다.  
```
Task Monitor -> Enable Task Monitor
```

### 2. 작업 모니터의 우선 순위 설정

menuconfig에는 Task Monitor의 우선 순위를 나타내는 **CONFIG_TASK_MONITOR_PRIORITY** 값을 설정합니다.  
```
Task Monitor -> Priority of Task Monitor
```

등록된 작업보다 작업 모니터의 우선 순위를 높게 설정하는 것이 좋습니다/pthread.  
그렇지 않으면 작업 모니터가 작동하지 않을 수 있습니다.  

예를 들어 task/pthread의 우선 순위가 100이고 작업 모니터의 우선 순위가 90인 경우 작업 모니터가 예약되지 않고 교착 상태가 지속될 수 있습니다.

### 3. 살아있음을 확인하는 간격 설정(초)

menuconfig에는 **CONFIG_TASK_MONITOR_INTERVAL** 살아 있는지 확인하는 간격(초)을 나타내는 값을 설정합니다.  
```
Task Monitor -> Interval for checking alive(sec) -> change a number over 0
```

작업 모니터는 이 간격마다 등록된 작업/pthreads의 상태를 확인합니다.  

예를 들어 사용자가 CONFIG_TASK_MONITOR_INTERVAL를 5초로 설정하고 7초 간격으로 작업/pthread를 등록하면 작업 모니터는 7초가 아닌 10초 후에 등록된 작업/pthread의 상태를 확인합니다.
	
### 4. 살아있음을 확인하는 최대 간격 설정(초)

menuconfig에는 **CONFIG_TASK_MONITOR_MAX_INTERVAL** 살아 있는지 확인하는 최대 간격(초)을 나타내는 값을 설정합니다.  
```
Task Monitor -> Max interval for checking alive(sec) -> change a number over CONFIG_TASK_MONITOR_INTERVAL
```

사용자가 등록할 수 있는 최대 간격으로 TASK_MONITOR_INTERVAL의 배수로 설정되어야 합니다.  
TASK_MONITOR_INTERVAL의 배수가 아닌 경우 이전 배수로 설정됩니다.  

예를 들어 TASK_MONITOR_INTERVAL를 5초로 설정하고 TASK_MONITOR_MAX_INTERVAL를 17초로 설정하면 TASK_MONITOR_MAX_INTERVAL는 15초로 변경됩니다.

<a id="use-task-monitor"></a>
## 작업 모니터 사용
태스크 모니터를 사용하기 위한 API 설명은 아래와 같습니다.

### 지원되는 API
헤더 파일 [task_monitor.h](../os/include/tinyara/task_monitor.h)는 아래와 같이 작업 모니터 관리를 지원하는 다음 API를 제공합니다.
```
int task_monitor_register(int interval);
void task_monitor_update_status(void);
```

작업 모니터에 등록하고 작업 상태를 업데이트하는 두 개의 API가 있습니다/pthread.  

**task_monitor_register(int 간격)** 함수는 현재 작업/pthread를 특정 간격으로 등록합니다.  
**task_monitor_update_status(void)** 함수는 현재 작업의 활성 상태를 업데이트합니다./pthread.

### 태스크 모니터의 예
사용자는 task_monitor_register(INTERVAL)를 사용하여 task/pthread를 등록하고 시간 간격이 만료되기 전에 task_monitor_update_status()를 사용하여 상태를 업데이트할 수 있습니다.
```
#include <tinyara/task_monitor.h>
#define INTERVAL 5
int main
{
    task_monitor_register(INTERVAL);
    ...
    task_monitor_update_status();
    ...
}
```
