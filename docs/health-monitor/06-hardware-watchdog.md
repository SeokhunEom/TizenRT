# 6단계: HW watchdog 연결

전제: 5단계가 확인·커밋됐고 사용자가 6단계 시작을 지시했다. [공통 진행 방식](README.md)과 [설계 문서](../HealthMonitorImplementationPlan.md)의 8.2절을 사용한다.

## 목표

실제 HW WDT 시작·keepalive와 health monitor 실행을 연결하고, 정상 절전과 장애 리셋을 확인한다. 이 단계에서 보드 의존적인 연결 사항을 해결한다.

## 구현 범위

1. 보드 초기화, HW WDT 시작, 기존 장치 드라이버 초기화의 순서를 확인하고 필요한 최소 연결을 구현한다.
2. `WATCHDOG_FOR_IRQ`를 사용하는 구성과 timeout 값을 검증용 설정에 반영한다. 기존 초기화가 이를 덮어쓰는지 확인한다.
3. 정상 tick, 검사 연기, 만료 확정 각각에서 keepalive를 수행하는 조건을 구체화하고 연결한다. 별도의 100ms SW 장애 판정은 만들지 않는다.
4. 정상 sleep 중 WDT 카운트·갱신 동작을 확인하고 기존 PM wakeup과 필요한 연동을 구현한다.
5. SW timeout 후 기존 PANIC이 완료되거나 진행되지 못하는 경우의 HW 리셋 경로를 확인한다.

예상 변경 위치: amebasmart watchdog·보드 초기화, tick keepalive 연결, 필요한 PM 보완. 기준 defconfig의 최종 제품 활성화는 통합 검증과 함께 7단계에 둔다.

하드웨어 제약 때문에 합의한 동작을 바꿔야 한다면 근거와 가장 작은 변경안을 사용자에게 제시한다. 앱이나 다른 HW WDT 사용자가 있는지도 확인해 변경 영향을 설명한다.

## 이 단계 완료 시 동작

실제로 시작된 HW WDT가 의도한 keepalive 경로와 연결된다. CPU0 tick 중단과 정상 sleep을 구분해 검증하며, CPU1만의 IRQ 마스킹까지 독립적으로 감시한다고 확장하지 않는다.

## 검증

- 설정 활성화뿐 아니라 실제 WDT 시작과 적용 timeout을 확인한다.
- CPU0 tick/keepalive 중단 시 HW 리셋과 reboot reason을 확인한다.
- 정상 tick과 정상 sleep에서 불필요한 HW 리셋이 발생하지 않는지 확인한다.
- 검사가 잠금 경합으로 연기되는 경우의 keepalive 동작을 확인한다.
- SW timeout의 신규 reason, PANIC, HW fallback 결과를 구분해 기록한다.

## 사용자 확인 항목

- WDT는 누가 언제 시작하고 갱신하며, 어떤 조건에서 갱신을 멈추는가?
- 선택한 timeout과 sleep 연동이 실제 시스템 사용 방식에 맞는가?
- 기존 watchdog 사용 방식이나 보드 초기화에 예상하지 못한 변경이 있는가?

완료 기준: 위 질문에 실제 호출 경로와 검증 결과로 답할 수 있다. 보드 검증이 미실행이면 HW 리셋 동작 확인 완료로 표시하지 않는다. 사용자 확인 후 커밋한다.

## 구현·검증 결과

### 시작 시점과 단일 소유권

기준 커밋은 5단계 `3ea8a0e8e`다. 기존 코드는 `up_wdog_init()`에서 `watchdog_init()`만 호출하고 실제 enable은 하지 않았다. 이후 `board_initialize()`가 장치 드라이버를 초기화하면서 timeout을 5,000ms로 다시 설정했다.

변경 후 IRQ watchdog 구성의 호출 경로는 다음과 같다.

1. [ARM 초기화](../../os/arch/arm/src/armv7-a/arm_initialize.c)의 `up_initialize()`에서 system timer를 초기화한 뒤 `up_wdog_init(CONFIG_WATCHDOG_FOR_IRQ_INTERVAL)`을 호출한다.
2. [RTL8730E watchdog 포트](../../os/arch/arm/src/amebasmart/amebasmart_watchdog_lowerhalf.c)가 timeout을 설정하고 early interrupt를 끈다. `watchdog_start()` → `WDG_Enable(WDG4_DEV)`로 실제 enable 키를 기록한다. 초기화가 끝났는지 저장하여 중복 호출로 재설정하지 않는다.
3. 이후 [보드 초기화](../../os/board/rtl8730e/src/rtl8730e_boot.c)는 IRQ watchdog 구성에서 장치 드라이버 초기화를 건너뛴다. 해당 구성의 `amebasmart_wdg_initialize()`도 `-EBUSY`를 반환하므로 다른 초기화 호출이 timeout을 덮어쓰지 못한다.

WDG4는 CA32의 non-secure watchdog이다. [Realtek 공식 설명](https://ameba-aiot.github.io/ameba-iot-docs/linux/en/latest/rst_linux/8_wdg/1_wdg_toprst.html)과 로컬 HAL의 `ARM_CORE_CA32` 선택, 실제 ARM 객체의 주소 `0x410004c0`를 함께 확인했다. 공식 Linux 문서의 드라이버 설정을 이 TizenRT 포트에 그대로 적용한 것은 아니다.

**`CONFIG_WATCHDOG_FOR_IRQ=y`인 RTL8730E에서는 `/dev/watchdog0`를 등록하지 않는다.** WDG4를 tick 감시가 전용으로 사용하며, 앱이 ioctl로 timeout·capture 모드를 바꾸거나 별도로 keepalive하는 경로를 노출하지 않는다. 해당 옵션이 꺼져 있으면 기존 장치 초기화·드라이버 경로를 유지한다. 드라이버 상위 계층, 다른 SoC의 초기화 코드는 바꾸지 않았다.

직접 사용자를 검색한 결과 `apps/examples/watchdog`, `apps/examples/reboot_reason_test`, `apps/examples/testcase/le_tc/drivers/tc_watchdog.c`가 기존 장치 ioctl을 사용한다. 이 예제·시험은 IRQ watchdog 전용 구성에서 함께 사용할 수 없다. 기준 defconfig에서는 해당 예제가 활성화돼 있지 않으며 제품 defconfig 자체는 이번 단계에서 변경하지 않았다. 다른 코어의 WDG2/IWDG는 이번 소유권 변경 대상이 아니다.

### timeout과 keepalive 조건

검증용 설정은 [rtl8730e-step6.config](../../os/kernel/health_monitor/tests/rtl8730e-step6.config)다. `CONFIG_HEALTH_MONITOR=y`, `CONFIG_WATCHDOG_FOR_IRQ=y`, interval **5,000ms**와 기준 PM/reboot reason 설정을 사용한다. 5초는 검증 시작값이며 제품의 IRQ 마스킹·초기 부팅·PANIC 진단 소요시간을 실측해 선택한 최종 값이 아니다.

현재 vendor `WDG_Init()`은 `timeout_ms / 1000 * 348`을 reload 값으로 사용한다. 따라서 RTL8730E IRQ interval은 **1,000~65,000ms의 정수 초**로 제한하고 범위 밖·비정수 초 설정은 컴파일에서 거부한다. `up_wdog_init(uint16_t)` 인자 범위도 넘지 않는다. 5초 설정의 prescaler는 `0x5d`, reload는 `1740`, window는 `0xffff`다. 이 값의 레지스터 쓰기는 확인했지만 실제 리셋까지의 정확한 시간은 발진기 오차와 HW 동작을 포함하므로 미실측이다. 기존 일반 앱 드라이버의 timeout 변환은 수정하지 않았다.

[health_monitor_timer()](../../os/kernel/health_monitor/health_monitor.c)는 이제 검사 진행 여부를 반환한다. [system tick](../../os/kernel/sched/sched_processtimer.c)은 다음 조건에서만 HW keepalive를 호출한다.

| 이번 tick의 감시 결과 | HW keepalive |
|---|---|
| CPU0, 안정된 빈 목록 또는 아직 도래하지 않은 예약 | 수행 |
| CPU0, 도래 후보의 최신 deadline 확인·필요한 재정렬을 정상 완료 | 수행 |
| 게시 중 사본(`-EAGAIN`) | 건너뜀 |
| 전용 잠금 점유 또는 weak CAS 실패로 검사 연기 | 건너뜀 |
| 최신 deadline 만료 확정 | reason 기록·PANIC으로 진행하며 수행하지 않음 |
| CPU1 | 수행하지 않음 |

순서는 `clock_timer()` → 감시 판단 → 조건부 `up_wdog_keepalive()` → 기존 scheduler/watchdog 처리다. keepalive를 위해 감시 잠금을 다시 얻지 않는다. 잠금 경합이 풀리고 다음 검사가 정상 완료되면 갱신을 재개한다. 갱신이 계속 중단되면 마지막 성공 갱신부터 HW timeout이 진행한다. 별도의 100ms SW 오류 판정·경합 횟수 제한·worker는 없다. 아직 미래인 예약의 빠른 경로에서 모든 감시 잠금 소유자의 진행까지 검사하는 것은 아니다.

감시 기능 없이 `WATCHDOG_FOR_IRQ`만 켠 구성의 기존 tick 선두 keepalive는 유지한다. RTL8730E 포트 자체도 CPU1의 keepalive를 무시한다. 실제 system timer는 CPU0에서 실행되므로 CPU1만의 IRQ 마스킹을 독립 감시한다고 주장하지 않는다.

### 정상 sleep과 wakeup 예산

vendor `watchdog_stop()`은 하드웨어를 정지하지 않는다. 따라서 PM에서 STOP/PAUSE 호출이 성공했다고 가정하는 연결은 만들지 않았다. `up_watchdog_disable()`도 IRQ 전용 구성에서는 WDT를 정지·갱신하지 않는다.

`ARCH_HAVE_WDOG_WAKEUP`을 지원하는 RTL8730E에서 [up_wdog_getwakeupdelay()](../../os/arch/arm/src/amebasmart/amebasmart_watchdog_lowerhalf.c)가 마지막 실제 시작/갱신 이후 **32-kHz SYSTIMER** 경과를 읽는다. system tick이 마스킹된 suspend 준비 시간도 이 계산에 들어간다. 32비트 뺄셈으로 counter wrap을 처리하며, 경과 ms는 올림·남은 system tick은 내림하여 보수적으로 계산한다. PM과 timer가 CPU0에서 로컬 IRQ를 끈 상태로 접근하므로 새 공용 잠금은 필요하지 않다.

이 capability는 Kconfig에서 SoC가 선택한다. `.config`를 직접 편집하거나 오래된 설정을 복사하여 선택을 누락하는 경우도 고려해, PM+IRQ WDT인데 capability가 없으면 포트 컴파일을 거부한다. 검증 overlay에는 직접 병합을 위해 명시했다.

- 예약 가능한 sleep 시간은 `명목 WDT interval / 2 - 마지막 갱신 후 경과 시간`이다. 절반은 wakeup·clock tolerance·tick 재개를 위한 여유로 남긴다.
- PM은 기존 SW watchdog, Health Monitor 검사 예약, 이 HW 예산 중 가장 빠른 값을 사용한다. suspend와 CPU 정지 후에도 다시 계산한다.
- 예산이 소진됐거나 기존 PM 진입 임계값보다 가까우면 sleep을 보류한다. 타이머나 누락 시간 보정 기능이 없는 구성도 활성 WDT가 있으면 sleep을 보류한다.
- PM 경로에서 WDT를 갱신하지 않는다. sleep 복귀 후에도 실제 tick의 정상 검사만 다음 갱신을 허용한다. 감시 deadline의 기존 sleep 시간 포함·늦은 KICK 정책은 그대로다.

5초 설정에서는 다른 예약이 없어도 마지막 갱신 후 **최대 2.5초 이내**로 wakeup을 예약한다. suspend 준비에서 2초가 지나면 남은 예약은 0.5초다. WDT가 sleep 중 계속 카운트하는 경우에도 대응하도록 선택한 정책이며, 전력 소모 측면에서는 장시간 연속 sleep이 줄어든다. 이 50% 여유가 실제 보드의 모든 wakeup 지연을 감당한다고 실측한 것은 아니다. WDG4의 CG/PG 중 실제 카운트·상태 보존·클록 동작 확인도 남아 있다.

### PANIC과 reboot reason

WDG4는 reset-only로 시작하므로 CPU0가 IRQ를 처리하거나 early IRQ handler를 실행해야만 리셋되는 구조가 아니다. SW 만료 확정 후 reason 62를 기록하고 기존 PANIC으로 진행하며, 이 tick에서는 keepalive를 하지 않는다. PANIC이 공용 잠금·다른 CPU pause·진단 출력에서 멈춰도 WDT를 갱신하는 별도 경로는 추가하지 않았다. 반대로 HW timeout이 진단 출력을 끝까지 기다려 주는 것도 아니므로 출력이 중간에 끊길 수 있다.

기존 [RTL8730E reboot decoder](../../os/arch/arm/src/amebasmart/amebasmart_reboot_reason.c)는 backup register의 SW reason을 HW reset flag보다 우선한다. 따라서 저장된 reason이 없는 순수 WDG4 reset은 **54**, SW reason **62가 기록된 뒤** PANIC 정체로 WDG4 reset이 발생하면 62가 보존된다. reason 62만으로 실제 reset 실행자가 SW autoreset인지 HW fallback인지 구별할 수는 없다. reason 기록 전에 멈추거나 다른 값이 이미 저장된 경우까지 무조건 62라고 주장하지 않는다.

### 실행한 검증

원본 제품 설정을 바꾸지 않고 임시 트리와 산출물을 `/tmp/health-monitor-step6.el2GE1`에 분리했다.

| 검증 | 결과와 확인 범위 |
|---|---|
| Linux AArch64/GCC 5.4, ASan/UBSan | 기존 12종 + tick/WDT SMP·UP·reason OFF 3종 + 실제 WDT/PM SMP·UP·timed wakeup 없음·tick suppression 없음 4종, 총 **19종 통과** |
| timeout 컴파일 경계 | 1,000/65,000ms 허용, 0/999/1,001/1,500/65,001/65,536ms는 지정한 interval 오류로 거부 |
| 누락된 PM 연결 방어 | PM+IRQ WDT에서 `ARCH_HAVE_WDOG_WAKEUP` 누락 시 지정한 오류로 컴파일 거부 |
| ARM GCC 10.3.1 kernel/PM archive | 기준 보호 모드·SMP에서 감시+IRQ WDT ON/OFF 빌드 통과. OFF에는 감시·IRQ WDT 참조 없음 |
| 변경 소스 엄격 컴파일 | 감시·tick·PM 및 IRQ WDT 포트에 `-Wextra -Werror` 추가해 통과. ON, UP/FIFO, reason OFF, PM 기능 제한, 감시만 OFF 구성의 객체도 확인 |
| 초기화·호환 경로 | 실제 `arm_initialize.o`, 보드 초기화 ON/OFF, 기존 장치 포트 OFF 객체 컴파일 통과. 보드 ON에는 `amebasmart_wdg_initialize` 참조가 없고 OFF에는 유지 |
| ARM 심볼/생성 코드 | 초기화의 `watchdog_init`·reset-only 설정·`watchdog_start`, CPU0 keepalive 조건, tick의 감시 반환값 분기 확인. IRQ 전용 포트에는 device 등록·할당·STOP 참조 없음 |

전체 archive에는 기존 kernel 경고 3개, PM 경고 6개가 ON/OFF에서 동일하게 남았다. 보드 초기화의 기존 I2C prototype 관련 경고 1개와 arch Makefile의 중복 `up_crc32.o` 대상 경고도 남아 있다. 변경한 코드의 엄격 컴파일은 경고 없이 통과했다. UP/RR의 기존 `sched_process_timeslice(int cpu)` 미사용 인자 경고 때문에 UP 추가 엄격 검증은 **RR interval 0의 FIFO 구성**으로 분리했으며, UP/RR까지 경고 없이 통과했다고 주장하지 않는다.

새 [watchdog 호스트 테스트](../../os/kernel/health_monitor/tests/watchdog_test.c)는 실제 PM·감시 구현, RTL8730E watchdog 포트, vendor `wdt_api.c`·ROM register helper, 실제 reboot reason decoder를 포함한다. MMIO 저장소·하드웨어 시계·IRQ/scheduler·backup register는 모형이다. continuous-count 모형으로 정상 sleep 및 keepalive 중단을 구분하며, 다음 내용을 확인한다.

- enable 키·reload·prescaler·window와 early interrupt 비활성, 중복 초기화 및 앱 장치 초기화에 의한 변경 방지, CPU1의 refresh 거부.
- tick이 고정돼 있어도 진행하는 HW wakeup 예산, half-budget 소진, SYSTIMER wrap, 세 종류 예약의 최솟값, callback 부재 및 긴 suspend 뒤 sleep 보류.
- PM 자체가 WDT를 갱신하지 않음, sleep 시간이 감시 deadline을 연장하지 않음, CPU0 keepalive 중단의 모델 reset 및 SW PANIC 정체 뒤 reason 62 보존.
- 별도의 실제 tick 함수 테스트에서 빈/미래/재정렬 완료만 feed하며, 게시 중·잠금 점유·CAS 실패·CPU1·만료 때는 feed하지 않음. 경합 해소 후 정상 진행 시 feed 재개.

재현할 호스트 명령은 다음과 같다. Linux에서 실행하면 기존 pthread barrier를 사용하는 테스트까지 포함된다.

```sh
make -C os/kernel/health_monitor/tests test OUT_DIR=/tmp/health-monitor-step6-tests
```

ARM 검증은 기준 `rtl8730e/loadable_ext_ddr_st7785` 임시 구성에 위 overlay를 병합하고 헤더를 재생성한 뒤 수행했다. 생성 헤더를 바꾼 추가 구성은 객체 컴파일이며 각 구성의 전체 펌웨어 빌드는 아니다. 정확한 절차는 산출물의 `build-arm.sh`, `arm-check.mk`에 있다. 주요 증거는 `host-linux-final.log`, `linux/watchdog-config.ok`, `linux/watchdog-invalid-*.log`, `kernel-{on,off}.log`, `pm-{on,off}.log`, `arm-strict-*.log`, `port-strict-*.log`, `board-{on,off}-symbols.txt`, `watchdog-on.asm`, `sched-processtimer-on.asm`이다.

Standards 및 Spec 독립 읽기 리뷰에서 잔여 지적은 없었다. 테스트 헤더의 오래된 설명은 현재 watchdog/CPU seam에 맞게 정리했다.

### 남은 보드 확인과 제출 범위

**이 단계에서 실제 보드의 HW 리셋을 관찰한 것은 아니다.** 전체 firmware link·flash·boot, 전원/리셋 핀·UART로 확인하는 물리 reset, 실제 CG/PG sleep 및 복귀 시간 측정은 실행하지 않았다. 19종 sanitizer 통과는 실행한 호스트 경로의 메모리·미정의 동작 검증이며 물리 하드웨어 증명이 아니다.

제품 활성화 전 필요한 보드 확인은 다음과 같다.

1. IRQ WDT 시작 이후 초기 부팅 구간이 선택한 timeout 안에 완료되는지, 실제 적용 timeout이 레지스터/실측과 일치하는지 확인한다.
2. CPU0 IRQ/tick 중단으로 WDG4 reset과 reason 54를 관찰한다. CPU1만의 마스킹은 별도 동작 범위로 시험한다.
3. 반복 CG/PG sleep에서 timer0·WDG4 상태 보존과 카운트를 확인하고, 준비/복귀 지연을 포함한 50% 여유 및 소비 전력을 측정한다.
4. SW timeout의 기존 PANIC/autoreset과, reason 62 기록 후 PANIC 정체의 HW fallback을 나누어 UART·물리 reset·reboot reason을 확인한다.

기존 vendor refresh의 `WDG_Wait_Busy()` register polling을 포함한 실제 tick 비용도 보드에서 측정해야 한다. 이 단계에서는 새 공용 잠금이나 대기 worker를 추가하지 않았으나 HW register 접근 지연까지 호스트 검사로 증명하지는 않는다.

커밋 파일은 watchdog 포트·보드 초기화, 감시 반환 계약·tick, PM hook·Kconfig·arch 선언, 테스트/검증 overlay, 단계 문서다. 기준 defconfig의 최종 활성화는 7단계에 남겨 두었다.

6단계 구현·검증과 독립 리뷰 후 사용자가 커밋과 origin push를 승인했다. 본 커밋 `health_monitor: connect tick progress to hardware watchdog`으로 6단계 구현을 완료하며, 7단계는 별도 지시를 기다린다.
