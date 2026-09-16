# 5단계: PM wakeup 연동

전제: 4단계가 확인·커밋됐고 사용자가 5단계 시작을 지시했다. [공통 진행 방식](README.md)과 [설계 문서](../HealthMonitorImplementationPlan.md)의 7절을 사용한다.

## 목표

정상 sleep 시간을 timeout에 포함하고, health monitor의 다음 검사 예약을 PM wakeup 계산에 반영한다.

## 구현 범위

1. PM이 최소 검사 예약을 조회할 수 있도록 기존 게시 상태를 이용한 최소한의 내부 연결을 추가한다.
2. 기존 wakeup 예약과 health monitor 예약 중 더 이른 시각을 선택한다. 무예약과 실제 0 지연을 구분한다.
3. 이미 검사할 시각이 됐으면 sleep 진입을 보류한다. 가까운 wakeup에 대한 기존 PM 기준도 적용한다.
4. sleep 복귀 시 기존 시스템 시간 보정을 사용하고 health monitor deadline 자체는 연장하지 않는다.
5. PM이 공용 잠금을 보유하는 경로에서 새로운 잠금 순서 문제를 만들지 않는지 확인한다.

예상 변경 위치: pm_idle.c, 감시 모듈의 최소 예약 조회. PM에서 전체 deadline 재탐색이나 힙 재정렬을 추가하지 않는다.

## 이 단계 완료 시 동작

health monitor 때문에 더 이른 wakeup이 필요하면 반영된다. 오래된 검사 예약 때문에 일찍 깨는 것은 허용하며, 복귀 후 검사 전 KICK도 계속 허용한다. HW watchdog의 sleep 동작 검증은 6단계에서 진행한다.

## 검증

- 기존 wakeup만 있음, health monitor 예약만 있음, 둘 다 있음, 둘 다 없음의 선택 결과를 확인한다.
- 검사 시각이 이미 됐거나 PM 진입 기준보다 가까운 경우 sleep 진입을 확인한다.
- 실제 sleep·복귀에서 시스템 시간과 deadline이 합의한 방식으로 처리되는지 확인한다.
- KICK 때문에 검사 예약이 오래된 경우 이른 wakeup 후 정상적으로 재정렬되는지 확인한다.
- 복귀 후 검사 전 KICK과 KICK 중단의 결과를 각각 확인한다.

## 사용자 확인 항목

- 기존 PM 동작 중 무엇이 바뀌었으며 어느 시각에 깨는가?
- 정상 sleep 시간이 timeout에서 제외되거나 deadline이 자동 연장되지 않는가?
- PM 연결을 위해 불필요한 전체 탐색이나 잠금 대기가 추가되지 않았는가?

완료 기준: wakeup 선택 규칙과 정상 sleep·복귀 결과를 제시했다. 실제 보드에서 확인하지 못한 항목은 명시하고 사용자 확인 후 커밋한다.

## 구현·검증 결과

### 구현 범위와 wakeup 선택

[pm_idle.c](../../os/pm/pm_idle.c)의 기존 PM 경로에 4단계의 `health_monitor_next_check()`를 연결했다. 새 API·저장공간·worker는 추가하지 않았다. 조회는 게시된 count/최소 예약 사본만 읽으며 감시 잠금을 얻거나 힙·TCB를 순회하지 않는다. 기존 watchdog 조회의 공용 잠금 사용은 그대로다.

| 기존 watchdog 예약 | Health Monitor 예약 | 선택 결과 |
|---|---|---|
| 없음 | 없음 | 기존과 같이 wakeup 타이머를 새로 설정하지 않음 |
| 있음 | 없음 | watchdog 지연 |
| 없음 | 미래 예약 있음 | 감시 예약까지의 지연 |
| 있음 | 미래 예약 있음 | 두 지연 중 작은 값 |
| 관계없음 | 도래·과거 예약 또는 게시 중(`-EAGAIN`) | sleep 보류, 다음 tick/PM 기회로 진행 |

감시 예약은 `(int32_t)(check_at - (uint32_t)clock_systimer())`로 지연을 계산한다. tick wraparound 및 `check_at == 0`을 지원하며, 실제 0 지연은 sleep 보류다. 무예약 여부는 조회 반환값으로 구분한다. 양수인 선택 지연에는 기존 `CONFIG_PM_SLEEP_ENTRY_WAIT_MS` 기준을 적용한다. 기준 RTL8730E의 1ms tick/10ms 기준에서 9 tick은 보류하고 10 tick은 허용한다. 기존 watchdog API의 0=무예약 표현은 바꾸지 않았다.

장치 suspend와 secondary CPU 정지 이후 예약을 다시 조회한다. RTL8730E는 systick 정지 시 ARM timer의 미처리 tick/소수 tick과 sleep 중에도 진행하는 32kHz 카운터를 기록한다. 이 보드의 `get_elapsedtick`은 준비·sleep·복귀를 포함하는 누적 system tick을 반환한다. 최종 조회는 멈춰 있는 OS 시계 기준 지연에서 이미 흐른 시간을 빼며, 도래했다면 sleep을 보류한다. 기존 소프트웨어 watchdog에도 같은 보정을 적용하므로 감시 기능 OFF일 때도 최종 조회를 수행한다.

활성 감시가 있는데 timed wakeup/tick suppression 설정 또는 `sleep`, `set_timer`, `get_elapsedtick` 콜백이 없으면 sleep을 보류한다. 감시 등록이 없으면 기존 PM 동작을 허용한다. 기준 RTL8730E는 해당 설정과 콜백을 모두 갖추고 있다. 이는 지원하지 않는 sleep 경로에서 감시 시간만 멈추는 것을 방지하는 최소 연결이며, Kconfig의 활성화 범위나 제품 defconfig는 바꾸지 않았다.

긴 timeout은 board API의 `unsigned int` microsecond 범위를 넘을 수 있다. 변환은 64비트 곱셈 후 `UINT_MAX`로 제한하여 overflow 대신 이른 wakeup을 선택한다. 32비트 unsigned/1ms tick 기준 한 번에 약 71.6분까지 설정하며, 깬 뒤 남은 예약을 다시 계산한다. health monitor의 deadline은 변경하지 않는다. 같은 경로를 쓰는 긴 watchdog 예약도 안전하게 변환하며, `clock_t` 지연을 signed 반환값으로 변환할 때는 `INT_MAX`로 제한한다. UP 엄격 컴파일을 위해 SMP에서만 쓰는 CPU snapshot 변수도 해당 설정 안으로 옮겼다.

### sleep 시간과 복귀 후 판정

누적 측정 콜백을 지원하는 보드는 장치 suspend 전에 systick을 정지하고, 장치 복귀까지 마친 뒤 전체 경과 tick을 한 번 보정한다. 준비 중 보류·timer 설정 실패·sleep 실패에도 같은 측정을 사용하므로 이전 sleep 표본을 소비하지 않는다. ARM timer는 마지막 보정 표본의 다음 tick 경계에서 재개하여 소수 tick과 표본 이후의 지연을 다음 ISR이 처리한다. `get_elapsedtick`이 없는 기존 보드는 실제 sleep 호출 뒤 기존 `get_missingtick` 보정을 사용하며, 감시가 활성화된 sleep은 보류한다.

`enable_and_compensate_systick()`의 `clock_timer_nohz(missing_tick)` 및 `wd_timer_nohz(missing_tick)` 호출은 유지한다. 기존 `clock_timer_nohz()`가 시스템 시간을 누락 tick만큼 증가시키므로 정상 sleep 시간도 감시 timeout에 포함된다. deadline을 연장하거나 별도의 sleep 위반 이력을 기록하지 않는다.

예를 들어 tick 100에서 timeout 20으로 등록하고 sleep 복귀 시 시스템 시간이 125라면 deadline은 여전히 120이다. 검사 전에 KICK하면 145로 갱신되어 정상으로 처리하며, KICK하지 않으면 timer 검사에서 만료된다. KICK 때문에 최신 deadline보다 오래된 `check_at`으로 일찍 깨어도 PM은 힙을 재정렬하지 않는다. 다음 timer 검사가 최신 deadline을 확인해 재정렬한다.

### 실행한 검증

기준 커밋은 4단계 `93f49bf9b`다. 검증 산출물은 `/tmp/health-monitor-step5.RF4zID`에 분리했다. ARM 빌드는 이전 단계의 생성된 임시 트리를 별도 복사한 뒤 변경 소스를 반영했으며, 원본 작업 트리의 설정·defconfig에는 손대지 않았다.

| 검증 | 결과와 증명 범위 |
|---|---|
| Linux AArch64/GCC 5.4, ASan/UBSan | 기존 7종 + PM UP/SMP, timed wakeup 없음, tick suppression 없음, 감시 OFF 5종으로 총 **12종 통과** |
| macOS Clang, ASan/UBSan | 신규 PM 5종 실행 통과. 최종 전체 회귀 결과는 위 Linux 실행 기준 |
| RTL8730E ARM GCC 10.3.1 | 기준 보호 모드/SMP/PM 구성의 `libpm.a` 감시 ON/OFF 및 `libkernel.a` ON 빌드 통과 |
| PM 소스 엄격 컴파일 | 기준 ON/OFF, UP, timed wakeup 없음, tick suppression 없음 5개 구성에서 `-Wextra -Werror` 추가 후 통과. 후자의 3개는 생성 헤더를 바꾼 객체 컴파일이며 전체 제품 구성 빌드는 아님 |
| ARM 심볼·생성 코드 | PM 객체는 감시 함수 중 `health_monitor_next_check`만 참조. secondary CPU 정지 후 최종 조회, 그 뒤 systick 중지 순서 확인. 감시 OFF PM archive에는 감시 심볼 정의·참조 없음 |

PM archive ON/OFF에는 기존 `pm_procfs.c` 경고 6개가 각각 남았다. kernel ON에는 이전 단계와 같은 경고 3개가 남았다. 변경한 `pm_idle.c`의 엄격 컴파일은 경고 없이 통과했다.

[PM 호스트 테스트](../../os/kernel/health_monitor/tests/pm_test.c)는 실제 `pm_idle.c`, 감시 등록·게시·timer 구현을 포함하고 아래 경계를 확인한다.

- 예약 없음/한쪽만 있음/양쪽 있음/동일 예약, 기존 PM 진입 임계값 전후, 도래·과거 예약, 32비트 wrap 및 tick 0, 최대 timeout과 microsecond 변환 포화.
- 게시 중 사본의 보류, 감시 잠금이 이미 점유돼 있어도 PM 조회가 기다리지 않음, PM 선택 전후 힙·최신 deadline 불변.
- suspend 콜백에서 신규 등록·STOP·게시 중 상태 발생, CPU 정지 중 등록 변경, 최종 재조회에 따른 타이머 재선택/진입 보류 및 CPU·장치 복구.
- 최종 재조회로 sleep을 보류할 때 systick 중지·누락 시간 읽기·보정을 실행하지 않음.
- sleep 경과 tick의 시스템 시간 반영, deadline 불변, 늦은 복귀 뒤 검사 전 KICK 인정, KICK 중단의 PANIC, 오래된 예약으로 조기 wakeup 후 timer 재정렬.
- 필수 sleep 기능이 부족할 때 활성 감시는 sleep을 보류하며, STOP 후에는 기존 PM sleep이 가능함.

Linux 재현 명령:

```sh
make -C os/kernel/health_monitor/tests test OUT_DIR=/tmp/health-monitor-step5-tests
```

ARM 기본 명령은 생성된 임시 기준 트리에서 다음과 같다. 감시 ON/OFF 전환마다 config 헤더를 재생성하고 해당 archive를 clean 후 빌드했다.

```sh
cd <temporary-tree>/os
make -j1 include/tinyara/config.h
make -j4 -C pm TOPDIR="$PWD" EXTRADEFINES=-D__KERNEL__ libpm.a
make -j4 -C kernel TOPDIR="$PWD" EXTRADEFINES=-D__KERNEL__ libkernel.a
```

주요 산출물은 `host-linux.log`, `pm-on.log`, `pm-off.log`, `kernel-on.log`, `arm-strict-*.log`, `pm-idle-on.asm`, `pm-idle-on-symbols.txt`, `pm-off-symbols.txt`, `libpm-on.a`, `libpm-off.a`, `libkernel-on.a`다. `build-arm.sh`와 `arm-check.mk`에 정확한 컴파일 절차를 보관했다. Standards 독립 검토에서 새 테스트의 static 함수명 권고 1건을 반영했다. Spec 독립 검토에서는 수정이 필요한 동작 오류나 범위 확장을 발견하지 못했다.

### 검증 한계와 제출 범위

- 호스트의 board sleep·wakeup timer·누락 tick 보고·시스템 시계·scheduler/IRQ·watchdog 지연 조회는 대체 구현이다. 실제 PM 함수가 시간 보정을 호출하고 감시 로직이 그 시간을 사용하는 것을 확인했으며, 실제 보드 절전 자체를 실행한 것은 아니다.
- 실제 RTL8730E의 wakeup 정확도, suspend 오버헤드, 누락 tick 측정·보정 정확도, SMP hotplug 동작과 장애 복구는 미검증이다. 해당 보드는 누락 시간을 ms로 보고하므로 이번 판단의 기준은 기존 1ms tick 구성이다. 다른 tick 주기의 보드 시간 단위 계약까지 검증했다고 주장하지 않는다.
- 전체 펌웨어 링크·플래시·부팅 및 실기기 PANIC·리셋은 실행하지 않았다. HW watchdog의 정상 sleep 처리와 장애 리셋 연결은 6단계, 제품 설정 활성화와 보드 통합 실측은 7단계에 남겨 두었다.
- 이번 커밋 범위는 PM 경로, 감시 내부 주석, PM 호스트 테스트·빌드 목록 및 단계 문서다.

5단계 구현·검증과 독립 리뷰 후 사용자가 커밋을 승인했다. 본 커밋 `health_monitor: include deadlines in PM wakeup`으로 5단계를 완료하며, 6단계 구현은 별도 지시를 기다린다.
