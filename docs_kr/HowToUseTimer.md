# 타이머 사용법

타이머에는 소프트웨어 타이머와 하드웨어 타이머의 두 가지 유형이 있습니다.  
동작과 사용법이 다릅니다.  
소프트웨어 타이머는 메모리가 충분하면 타이머를 무제한으로 생성할 수 있지만 정확하지 않습니다.  
하드웨어 타이머는 정확하고 미세한 시간을 제공하지만 개수에 제한이 있습니다.

## 목차

- [소프트웨어 타이머](#software-timers)  
- [하드웨어 타이머](#hardware-timers)

<a id="software-timers"></a>
## 소프트웨어 타이머

소프트웨어 타이머에는 절전, 작업 대기열 및 POSIX 타이머의 세 가지 종류가 있습니다.  

시스템 틱과 함께 작동하므로 시간 확인 및 응답이 부정확해집니다.  
- 해상도
마이크로초, 나노초 슬립 기능이 있는데 틱 때문에  
최소 시간은 1틱입니다. TizenRT는 틱당 타이머를 설정하는 구성 CONFIG_USEC_PER_TICK를 제공합니다.  
CONFIG_USEC_PER_TICK를 10,000(10ms)로 설정하고 주어진 시간이 10ms 미만인 경우 10ms 후에 깨어납니다.  
- 부정확함
시스템 활동이나 통화 처리에 소요되는 시간으로 인해 절전 모드가 약간 길어질 수 있습니다. 시스템 타이머의 세분성으로  
.

소프트웨어 타이머는 특정 작업이 주어진 시간 이후 언제든지 실행될 수 있고 동시에 여러 번 실행될 수 있을 때 사용하는 것이 좋습니다.

### 수면

수면 기능에는 sleep, usleep, nanosleep의 세 가지가 있습니다.  
주어진 시간 동안 실행 중인 스레드를 일시 중지한 후 스케줄링으로 돌아와 다음 라인을 실행합니다.  
유일한 차이점은 아래와 같이 시간 분해능입니다.  
- 초 안에 잠들다  
- usleep in microsecond  
- nanosleep(나노초)

sleep과 nanosleep은 POSIX 호환 API이며 usleep은 Linux와 동일합니다.  
모든 프로토타입은 *unistd.h*에 있습니다.

> **NOTE**
> 
> POSIX에 의해 신호는 EINTR 반환 값을 사용하여 주어진 시간 전에 잠을 깨울 수 있습니다.

### 작업 대기열
<To be updated>

### POSIX 타이머
<To be updated>

<a id="hardware-timers"></a>
## 하드웨어 타이머

칩셋이 스스로 타이머를 구동하고 시간을 측정하기 때문에 고해상도를 제공하고 시간에 맞춰 작동한다.  
SW 측에서 작업량이 많은 경우에도 마찬가지입니다. 하지만 HW이기 때문에 개수에 제한이 있습니다.  
TizenRT는 VFS 기능을 통해 모든 보드에서 동일한 애플리케이션 사용을 유지하기 위한 타이머 드라이버를 제공합니다.

### 드라이버

VFS를 사용하기 때문에 타이머는 *open*, *read*, *write* 및 *ioctl* API와 함께 작동합니다.  
타이머 드라이버는 *os/drivers/timer.c* 파일입니다. 

하드웨어 타이머의 방식은 다음과 같습니다.
1. 타이머 장치 열기
```
int fd = open(<TIMER_DEVICE_PATH>, O_RDONLY);
```
2. 시간 만료 알림 설정
```
struct timer_notify_s notify;
notify.arg   = NULL;                  /* An argument to pass with the FIN */
notify.pid   = (pid_t)getpid();       /* The ID of the task/thread to receive the FIN */

ioctl(fd, TCIOC_NOTIFICATION, &notify);
```
3. 시간 설정
```
ioctl(fd, TCIOC_SETTIMEOUT, time);
```
4. 타이머 시작
```
ioctl(fd, TCIOC_START, 0);
```
5. *fin_wait*를 사용하여 알림을 기다립니다.
```
fin_wait();
```
6. 타이머 중지
```
ioctl(fd, TCIOC_STOP, 0);
```
7. 타이머 장치 닫기
```
close(fd);
```

*apps/examples/timer*에서 [타이머의 예](https://github.com/Samsung/TizenRT/blob/master/apps/examples/timer/timer_main.c)를 수정하세요.
