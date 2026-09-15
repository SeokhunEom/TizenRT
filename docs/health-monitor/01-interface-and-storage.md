# 1단계: 인터페이스·시간 표현·저장공간

전제: [공통 진행 방식](README.md)을 읽고 사용자가 1단계 시작을 지시했다.

## 목표

실제 감시 동작을 연결하기 전에 외부 호출 형태와 메모리 구성을 코드로 확인할 수 있게 한다. 기준은 [설계 문서](../HealthMonitorImplementationPlan.md)의 2~4절이다.

## 구현 범위

1. `CONFIG_HEALTH_MONITOR`를 기본 비활성으로 추가하고 필요한 조건부 빌드 구성을 마련한다.
2. 공개 ioctl 명령·인자·오류 규칙, 제안 장치 경로를 헤더로 정의한다. 내부 함수 선언과 공개 헤더의 노출 범위를 구분한다.
3. TCB에 조건부 `deadline`, `timeout` 필드를 추가하고 저장 위치를 숨기는 내부 접근 함수를 둔다. 정적 힙 항목의 타입과 최대 크기 기준도 정한다. 실제 힙 조작은 2단계에 둔다.
4. 시스템 시간 접근, 저장할 tick 폭, wraparound 비교, 최소·최대 timeout과 ms 변환 규칙을 정리한다. 코드에 필요한 상수·타입을 반영하고 선택 이유를 제출한다.

예상 변경 위치: 공개·내부 헤더, TCB 정의, kernel/driver Kconfig와 필요한 빌드 파일. 이후 단계에 필요한 함수는 선언으로 두며, 등록이나 KICK이 된 것처럼 성공을 반환하는 임시 구현을 추가하지 않는다.

## 이 단계 완료 시 동작

TCB와 호출 규약을 확인할 수 있다. 드라이버 등록, 감시 대상 등록, timer 검사, PM·HW WDT 동작은 아직 연결되지 않는다.

## 검증

- 비활성 구성에서 기존 코드의 빌드를 확인한다.
- 활성 구성에서 현재 단계의 헤더·TCB·빌드 연결이 성립하는지 확인한다.
- TCB의 실제 추가 크기와 정적 힙의 예상 최대 크기를 계산한다.
- timeout 0, 지원 범위 경계, tick 변환과 wraparound 비교의 처리 규칙을 제시한다.

## 사용자 확인 항목

- 장치 경로, ioctl 인자, 내부 함수 형태가 의도한 사용 방식과 맞는가?
- timeout 범위와 시간 표현에 예상하지 못한 제약이 있는가?
- TCB 필드와 메모리 증가량이 허용 가능한가?

완료 기준: 위 결정이 실제 헤더·설정·TCB diff와 검증 결과로 제시됐다. 사용자 확인 후 이 범위만 커밋하고 대기한다.

## 구현·검증 결과 (2026-09-15)

### 반영한 결정

- [kernel/Kconfig](../../os/kernel/Kconfig): `CONFIG_HEALTH_MONITOR` 기본값은 `n`. 정기 timer ISR이 필요하므로 `SCHED_TICKLESS`, `SUPPRESS_INTERRUPTS`, `SUPPRESS_TIMER_INTS` 구성에서는 선택하지 못하게 했다. 기준 보드의 PM tick suppression은 제외 조건이 아니다.
- [공개 헤더](../../os/include/tinyara/health_monitor.h): 장치 경로 `/dev/health_monitor`, `HMIOC_START`, `HMIOC_KICK`, `HMIOC_STOP`을 정의했다. [ioctl 번호 공간](../../os/include/tinyara/fs/ioctl.h)에 기존 번호와 겹치지 않는 `0x3c00`을 예약했다. 명령 값은 각각 `0x3c01`, `0x3c02`, `0x3c03`이다.
- START 인자는 포인터가 아니라 `uint32_t` 범위의 ms 값을 `unsigned long`으로 전달한다. 예: `ioctl(fd, HMIOC_START, 1000UL)`. KICK·STOP 인자는 `0UL`이다. 오류, 호출 스레드 기준의 소유권, open·close 의미도 공개 헤더에 명시했다.
- [TCB](../../os/include/tinyara/sched.h): 기능이 켜졌을 때만 `uint32_t deadline`, `uint32_t timeout`을 포함한다. `timeout == 0`은 미감시다. 이후 전용 잠금 아래에서 접근하므로 `volatile`을 동기화 수단으로 사용하지 않는다.
- [내부 헤더](../../os/kernel/health_monitor/health_monitor.h): 내부 START/KICK/STOP 및 대상 TCB를 받는 cleanup을 선언했다. 힙 항목은 `TCB *`와 `uint32_t check_at`, 최대 항목 수는 `CONFIG_MAX_TASKS`다. 배열 할당과 힙 조작은 아직 없다.
- 사용자 검토 후 TCB 직접 저장을 유지하고 `health_monitor_state(tcb)`를 추가했다. 저장 위치에 대한 직접 접근은 이 함수 한 곳에 두고, cleanup 선언에는 PID 반납 전 정리와 task 재시작 시 등록 해제 규칙을 명시했다. 후속 구현은 [저장공간 접근 규칙](../HealthMonitorImplementationPlan.md#41-자료구조)을 따른다. 실제 종료 hook 연결은 2단계다.
- 이번 단계는 헤더와 설정만 추가하므로 별도 Makefile/Make.defs 또는 성공 stub은 만들지 않았다. 기준 defconfig도 바꾸지 않았다.

### 시간 규칙과 제약

시간은 `(uint32_t)clock_systimer()`로 읽도록 정했다. 별도 카운터 없이 PM 보정이 적용되는 시스템 시간의 하위 32비트를 사용한다. 실제 시간 읽기 호출과 PM 연결은 이후 단계에서 구현한다.

timeout 변환은 64비트 중간값으로 `ceil(timeout_ms * 1000 / USEC_PER_TICK)`을 계산한다. 기존 `MSEC2TICK()`의 반올림 대신 올림을 사용해 양의 짧은 timeout이 0 tick이 되는 것을 막는다. 변환 결과가 `1..INT32_MAX` tick이 아니면 헬퍼는 0을 반환하며, 이후 START에서 `-EINVAL`로 처리한다.

기준 보드는 1ms tick이므로 다음 범위를 사용한다.

| 입력 | 변환/정책 |
|---|---|
| 0ms | 무효 |
| 1ms | 1 tick, 최소 timeout |
| 2,147,483,647ms | `INT32_MAX` tick, 최대 timeout(약 24.9일) |
| 2,147,483,648ms ~ `UINT32_MAX` ms | 무효 |

deadline은 32비트 모듈로 덧셈을 사용하고, 시각 비교는 부호 있는 차이로 wraparound를 처리한다. 시각 0은 유효하며, 두 시각의 간격이 `2^31` tick 이상이면 이 비교로 순서를 보장하지 않는다. 특히 deadline 경과 후에도 `2^31` tick 이상 검사하지 못하는 상황은 지원 범위 밖이다.

힙에는 이미 지난 예약과 먼 미래 예약이 함께 있을 수 있다. 따라서 2단계 정렬에서는 두 예약 tick을 직접 빼는 대신 **동일한 now에 대한 signed offset**을 비교한다. 이는 최대 timeout에 가까운 새 예약 때문에 이미 도래한 후보가 뒤로 밀리는 것을 방지하기 위한 시간 표현 규칙이다. 실제 힙 알고리즘은 이번 단계에 추가하지 않았다.

### 실제 크기

대상 설정, ARM Cortex-A32 ABI에서 컴파일한 객체의 심볼 크기로 확인했다.

| 구조체/저장공간 | 기능 OFF | 기능 ON |
|---|---:|---:|
| `struct tcb_s` | 232 B | 240 B |
| `struct task_tcb_s` | 252 B | 260 B |
| `struct pthread_tcb_s` | 320 B | 328 B |
| 향후 힙 항목 256개 | 없음 | 예상 2,048 B(항목당 8 B) |

task/pthread 크기는 각각 공통 TCB를 포함한 값으로, 표의 증가량을 중복 합산하지 않는다. TCB당 실제 증가는 8 B이며 256개 기준 2,048 B다. 향후 힙까지 포함하면 4,096 B + 소량의 전역 상태를 예상한다. 이번 단계에는 힙 배열을 아직 확보하지 않았다.

### 실행한 검증

원본 작업 트리에 활성 `.config`나 빌드 산출물을 만들지 않고 `/tmp/health-monitor-step1.isS8Pw` 복사본에서 검증했다. 컴파일러는 `arm-none-eabi-gcc 13.2.1`이다.

1. 기준 `rtl8730e/loadable_ext_ddr_st7785` 설정의 OFF 구성과, 여기에 `CONFIG_HEALTH_MONITOR=y`만 추가한 ON 구성에서 `make -j1 context` 후 보호 모드 커널 아카이브 빌드에 성공했다. 두 구성 모두 242개 객체를 포함한다.
2. ARM 컴파일에서 감시 상태 8 B, 힙 항목 8 B, 최대 항목 256개를 정적 검증하고 위 구조체 크기를 측정했다.
3. 공개 헤더를 사용하는 앱 형태의 코드를 C/C++로 OFF/ON 각각 컴파일했다. 명령 값과 공개 헤더에 내부 힙 상수가 노출되지 않는 것도 검사했다. 새 헤더 검증에는 `-Wall -Wextra -Werror`를 적용했다.
4. 실제 내부 시간 헬퍼를 포함한 호스트 테스트를 UBSan과 함께 실행했다. 1,000/10,000/500/1,500us tick 각각에서 0, 최소·최대 경계, 올림, 곱셈 overflow, wrap 전후 및 동일 시각을 검사했다. 각 구성마다 100,000개 입력·시각 샘플도 통과했다. 접근 함수 추가 후에는 실제 OS 헤더를 `-isystem os/include`로 포함하고 테스트의 assert 실패를 trap으로 처리해 보드 PANIC 의존성을 분리한 뒤 재실행했다.
5. 실제 `os/kernel/Kconfig`를 포함한 임시 테스트 구성에서 기본 OFF, 명시적 ON, timer IRQ 비활성, 전체 IRQ 비활성, tickless의 5가지 조건을 확인했다. 뒤의 세 경우에는 ON 선택이 차단됐다.
6. 호스트에서 실제 TCB 두 개로 접근 함수가 정확한 상태 주소를 반환하는지, 한 TCB의 deadline·timeout 갱신이 다른 TCB나 PID에 영향을 주지 않는지 확인했다. 상태 접근은 단일 스레드 테스트이며 SMP 동기화를 검증한 것은 아니다.
7. ARM `-Os` 컴파일 결과에서 접근 함수 호출이 `adds r0, #60` 주소 계산으로 인라인되는 것을 확인했다. 검증용 wrapper에는 주소 계산과 복귀 명령만 있으며 잠금·할당·추가 함수 호출은 없다. 접근 함수 추가 후 커널 OFF/ON 및 공개 C/C++ 헤더도 다시 빌드했고 TCB 크기는 위 표와 동일하다.
8. `git diff --check`와 커밋 대상 전체의 staged 공백 검사를 통과했다.

커널 빌드의 핵심 명령은 다음과 같다. OFF/ON 사이에 임시 복사본의 커널 산출물만 clean했다.

```sh
cd /tmp/health-monitor-step1.isS8Pw/os
./tools/configure.sh rtl8730e/loadable_ext_ddr_st7785
make -j1 context
make -j1 -C kernel TOPDIR="$PWD" clean
make -j4 -C kernel TOPDIR="$PWD" EXTRADEFINES=-D__KERNEL__ libkernel.a
```

위 임시 디렉터리에 최종 재검증 로그 `kernel-off-final.log`, `kernel-on-final.log`, `layout-final.log`, `time-state-results.log`, `accessor-arm.log`와 기존 `option-*.log`, 검증 소스 `layout.c`, `public.c`, `time_test.c`를 보관했다. 임시 검증 파일은 제품 소스나 커밋 범위에 포함하지 않는다.

### 검증 제한과 남은 작업

- 전체 `Kconfig`의 `kconfig-conf --olddefconfig`는 다른 보드의 기존 `/root/tizenrt/...` 절대경로 참조로 실패했다. 위치는 `os/board/bk7239n/src/middleware/driver/Kconfig:2`이며 해당 파일은 수정하지 않았다. 커널 옵션 분리 검증은 전체 보드 Kconfig 검증을 대체하지 않는다.
- 커널 OFF/ON 빌드에는 기존 소스에서 같은 6개 경고가 있다. 새 헤더의 단독 컴파일은 경고 없이 통과했다.
- 전체 펌웨어 링크, 부팅, SMP 경합, 실제 timeout/PANIC·PM·HW WDT 보드 검증은 수행하지 않았다. 해당 동작은 이후 단계 범위다.
- 사용자가 TCB 직접 저장과 내부 접근 함수 방식을 확인하고 1단계 마무리·커밋을 지시했다. 소스 변경 범위는 위 Kconfig, 공개/내부 헤더, 공통 ioctl 헤더, TCB 헤더의 5개 파일이며 기존 계획 문서와 이번 결과도 함께 포함한다. 커밋 메시지는 `health_monitor: define interfaces and task state`다.

1단계 커밋 후 대기한다. 다음 구현은 2단계 등록·갱신·해제 및 TCB 수명 연동이며 별도 시작 지시가 필요하다.
