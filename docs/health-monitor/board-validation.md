# RTL8730E Health Monitor 사용·보드 시험 절차

기준은 `rtl8730e/loadable_ext_ddr_st7785`, CPU 2개, tick 1ms, 최대 task 256개다. 이 문서는 시험 준비 절차다. **2026-09-18 현재 실제 보드가 없어 부팅·절전·리셋·실행 시간은 미측정이다.** 빌드와 호스트 결과는 [7단계 결과](07-integration-validation.md)에 별도로 기록한다.

## 설정과 빌드

사용자 지시에 따라 RTL8730E 대표 설정인 `loadable_ext_ddr_st7785/defconfig` 하나에 [활성화 설정](rtl8730e-health-monitor.config)을 적용했다. health monitor와 CPU0 tick 소유 WDG4를 켜고 watchdog 간격은 5000ms로 둔다. 검증 앱은 기본 OFF이며, 다른 RTL8730E defconfig는 변경하지 않았다. 설정 적용은 보드 실측 완료를 의미하지 않는다. 기준 설정의 PM, timed wakeup, tick suppression, reboot reason은 이미 켜져 있다.

WDG4 소유권이 tick으로 이동하면 `/dev/watchdog0`는 등록되지 않는다. 따라서 실제 제품 시험에서는 해당 장치의 open/keepalive/stop에 의존하는 코드가 있는지도 확인한다. 5000ms는 vendor 설정에 요청하는 간격이다. 실제 reset 지연과 CG/PG 중 watchdog 카운트·유지 여부는 아직 확인하지 않았다.

각 구성은 **새로운 임시 소스 복사본**에서 빌드한다. `.git`은 필요 없으며 `os`, `apps`, `external`, `framework`, `build`, `lib`, `tools`, `loadable_apps`, `resource` 디렉터리를 보존한다. 기존 생성물·`.config`가 있는 복사본을 재사용하지 않는다.

```sh
# 원본 저장소 루트에서 실행. COPY는 새 소스 복사본의 절대 경로다.
python3 docs/health-monitor/configure-validation.py "$COPY" off
# 다른 새 복사본에는 off 대신 on 또는 test 사용:
# off  = 활성화 전 비교 구성: HM/IRQ watchdog OFF, 기존 watchdog 장치 유지
# on   = 현재 대표 defconfig와 같은 활성 구성, 검증 앱 제외
# test = on + CONFIG_EXAMPLES_HEALTH_MONITOR=y

docker run --rm --pull never --platform linux/arm64 --network none \
  -v "$COPY:/work" -w /work/os \
  tizenrt/tizenrt:2.0.1-arm64-rtl8730e-local \
  bash -lc 'make -j1 JOBS=-j2' > build.log 2>&1
```

로컬 검증에 사용한 이미지 이름이다. 다른 환경에서는 ARM 컴파일러와 Realtek 후처리 도구의 의존성을 갖춘 이미지를 사용한다. 제한된 Docker 메모리에서는 구성별로 순차 실행하고 `JOBS=-j2`로 하위 make의 병렬 작업도 제한한다. `make -j1`을 사용한 이유는 이 저장소의 최상위 `post`가 `pass1/pass2`와 병렬 실행될 수 있기 때문이다. `make` 종료 코드와 로그, FIP, kernel/common/app1 TRPK를 함께 확인한다. `dbuild.sh`의 `tee` 파이프라인 종료 코드만으로 성공을 판정하지 않는다.

설정 스크립트는 Kconfig solver가 아니다. 고정된 기준 defconfig에 검토 가능한 설정만 덮어쓰며, 이미 설정된 소스에는 실행을 거부한다. `ARCH_HAVE_WDOG_WAKEUP`은 이 보드가 select하는 capability이지만 `configure.sh`가 select를 계산하지 않으므로 명시한다. `off`는 활성화 전 측정 구성을 재현하도록 기능과 해당 capability 표기를 제거한다. 일반 보드 capability 자동 생성기로 사용하지 않는다.

## 앱 호출 계약

`START`의 timeout은 포인터가 아닌 밀리초 값이다. 현재 호출하는 스레드가 감시 대상이며, fd를 공유해도 각 스레드의 상태를 갱신한다.

```c
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <tinyara/health_monitor.h>

int fd = open(HEALTH_MONITOR_DEVPATH, O_RDWR);
if (fd < 0) {
    /* 오류 처리 */
    return;
}
if (ioctl(fd, HMIOC_START, 2000UL) < 0) {
    close(fd);
    return;
}
/* 같은 스레드의 의미 있는 작업 진척 시 호출한다. */
if (ioctl(fd, HMIOC_KICK, 0UL) < 0) {
    /* 실패를 기록하고 작업 종료 정책을 적용한다. */
}
if (ioctl(fd, HMIOC_STOP, 0UL) < 0) {
    /* close는 STOP 대용이 아니다. 실패를 처리해야 한다. */
}
close(fd);
```

`close`만으로 감시가 해제되지 않는다. `STOP` 또는 스레드 종료 시 해제된다. `START` 중복은 `EEXIST`, 미등록 `STOP`은 `ENOENT`, 미등록 `KICK`은 성공하는 no-op이다. 기간은 tick 단위로 올림하며, API의 실제 범위는 [공개 헤더](../../os/include/tinyara/health_monitor.h)를 따른다.

timeout은 sleep 시간도 포함한다. **deadline이 지났더라도 timer 검사 전에 KICK이 받아들여지면 새 deadline으로 갱신한다.** 반대로 검사가 만료를 확정하면 PANIC한다. 앱에서 시간만 맞춰 이 경쟁의 순서를 보장할 수는 없다.

## 검증 앱 명령

`test` 펌웨어의 TASH에서 실행한다. 부팅 시 자동 등록은 없다. 예제는 timeout 1~60000ms, period < timeout, count 1~100000, rounds 1~1000으로 제한한다. 정상 시험은 스케줄 지연을 고려해 충분히 여유 있는 아래 값으로 시작한다. timeout을 아주 짧게 지정하면 정상 명령도 의도치 않게 만료될 수 있다.

| 명령 | 동작과 관찰 항목 |
|---|---|
| `health_monitor run 2000 250 20` | START 후 250ms마다 KICK 20회, STOP, `completed`. 실제 작업 진척 대신 지연을 사용하는 최소 예제 |
| `health_monitor stop 2000` | START→STOP 후 2100ms 대기. 원래 deadline을 지나도 생존 |
| `health_monitor exit 5000 100` | 매번 worker를 등록한 상태로 종료·join. 마지막 종료 후 5100ms 생존 확인 |
| `health_monitor smp 5000 100` | 공유 fd, CPU0/1 affinity, 두 worker 출발 gate. 각각 START/KICK/STOP 32회 후 다시 START한 채 종료. 매 라운드 join 후 다음 라운드 생성 |
| `health_monitor run 10000 6000 10` | 앱 KICK 사이 6초. PM 진입이 실제 발생하면 HW budget 때문에 중간에 깨어나 feed하고 다시 sleep할 수 있어야 함 |
| `health_monitor expire 2000` | START 후 누구도 post하지 않는 semaphore에서 대기. **의도적으로 PANIC/리셋 유발**, 성공 시 함수가 돌아오지 않음 |
| `health_monitor reason` | 재부팅 후 저장된 원인 조회. health timeout은 62, 순수 WDG4는 54 예상 |

TASH 명령은 비동기 실행된다. `completed` 또는 `FAILED` 로그를 확인한 후 다음 명령을 실행한다. `expire`는 리셋 시험용 펌웨어에서 마지막에 실행한다. 기본 5초 HW watchdog 때문에 PANIC 덤프 도중 리셋되어 UART 로그가 잘릴 수 있다.

`exit/smp`는 PID를 출력하지만 재사용을 강제하지 않는다. 로그에서 동일 PID가 다른 라운드에 다시 나오는지 확인한다. TCB 주소 재사용과 해제 시점은 별도 커널 관찰이 필요하다. Linux fixture의 task 종료 hook 모델은 실제 TizenRT scheduler 실행 증거가 아니다.

## 보드 시험 순서와 기록

먼저 보드 리비전, 공급 전원, 펌웨어 SHA, defconfig/overlay, 컴파일러, CPU clock, 실제 사용하는 sleep mode, UART 전체 로그를 기록한다. 다운로드는 보드의 기존 절차를 따른다. 아래 각 결과에 시작/종료 시각과 관찰 근거를 첨부한다. 리셋 직후에는 추가 수동 reboot 없이 `health_monitor reason`을 읽는다.

1. **부팅/무등록**: `test` 펌웨어로 TASH까지 부팅하고 5초보다 충분히 오래 idle 상태를 유지한다. `/dev/health_monitor` 접근과 `/dev/watchdog0` 부재, 무등록이어도 CPU0 tick이 WDG4를 갱신하는지 확인한다. 부팅 중 watchdog 시작 후 첫 tick까지의 지연도 측정한다.
2. **정상/STOP/종료/SMP**: 위 정상 명령들을 순서대로 실행한다. 각 명령의 완료, 예기치 않은 reset 부재, 반복 후 등록·fd 누수 부재를 확인한다. PID 재사용이 관찰되지 않았다면 재사용 시험은 미확인으로 남긴다. TCB 검사 시에는 임시 진단 빌드에서 생성/cleanup/반납 시점의 주소와 등록 여부를 RAM에 기록하고 worker 종료 후 출력한다.
3. **실제 절전**: `run 10000 6000 10`을 실행하고 불필요한 UART 입력·로그와 주변장치 activity를 줄인다. PM trace/보드 전류/외부 로직 분석기로 CPU가 정말 sleep했는지, CG/PG 중 어떤 경로였는지 확인한다. `nanosleep` 성공이나 앱 생존만으로 절전을 통과 처리하지 않는다. 각 지원 sleep mode에서 HW budget(마지막 feed부터 명목상 2.5초 이내)과 복귀 준비시간, deadline의 sleep 시간 포함 여부를 기록한다. 준비시간이 budget을 소진하면 진입을 포기해야 한다.
4. **sleep 중 만료**: 실제 sleep 진입 조건에서 `expire 2000`. PM에서 복귀한 뒤 만료를 감지하고 원인 62로 리셋하는지 확인한다. 짧은/긴 sleep, 외부 IRQ 조기 wakeup도 각각 기록한다.
5. **검사 전 늦은 KICK**: 자동 호스트 테스트는 순서를 통제한다. 보드에서 재현하려면 별도 시험 빌드에서 CPU0 clock은 계속 진행시키고 `health_monitor_timer()` 검사만 제한된 시간 미루는 one-shot gate가 필요하다. CPU1에서 old deadline 이후 KICK을 완료한 뒤 gate를 해제하면 새 deadline으로 생존해야 한다. KICK 없이 해제하면 PANIC해야 한다. 지연은 HW watchdog보다 짧게 제한한다. IRQ 정지나 debugger halt만으로 같은 시험이 된다고 간주하지 않는다. 이 임시 gate의 구현·실행은 아직 수행하지 않았다.
6. **HW fallback**: 별도 fault-injection 빌드에서 watchdog이 시작된 다음 CPU0의 tick/feed를 중단시킨다. 예를 들어 CPU0에 고정한 커널 시험 task가 로컬 IRQ를 끄고 복귀하지 않는 경로를 사용한다. SW reason을 쓰지 않고 reset 후 54를 확인한다. 다른 CPU tick이 feed를 대신하지 않는지도 본다. debugger halt는 watchdog도 멈출 수 있으므로 단독 근거로 사용하지 않는다. 이어서 health PANIC 이후 진행이 막힌 경로에서 HW가 reset하되 저장된 62가 유지되는지 확인한다. 해당 fault-injection 코드는 제품 앱에 포함하지 않았다.

## 실행 시간·잠금·메모리 측정

호스트 실행 시간은 RTL8730E ISR 비용으로 환산하지 않는다. 아래 수치는 모두 보드 미측정 상태다. 측정 시 timestamp를 고정 크기 RAM 버퍼에 기록하고 시험 후 출력한다. critical section/ISR 안의 `printf`, 동적 할당, scheduler lock 추가는 금지한다. probe 없는 펌웨어와도 비교해 계측 비용을 분리한다.

| 측정 | 조건과 기록할 값 |
|---|---|
| 평상시 tick | 무등록 / root가 미래 / stale root 보수. HM 함수와 전체 `sched_process_timer` 구간을 각각 측정; count, min/p50/p99/max cycles |
| KICK | CPU별 단독/동시 호출, syscall 포함 시간과 registry 구간 분리. 등록 수 1, 중간값, 가능한 최대 N; START/STOP/exit 동시 실행도 포함 |
| thread 잠금 대기 | `health_monitor_lock`의 IRQ save 전후, spinlock 획득 직후, unlock/IRQ restore 직후 timestamp. 대기/보유/IRQ masking 시간을 분리 |
| ISR contention | one-shot CAS 실패 시 반환 시간과 skip 횟수. ISR은 lock 대기를 하지 않아야 하며 실패 tick에서 feed하지 않아야 함 |
| 동시 도래 후보 | check_at이 같은 후보를 갱신해 실제 deadline은 미래인 상태로 검사, 전부 보수하는 비용 측정. 만료 PANIC 경로도 별도 측정 |
| 최대 256개 | 제품의 기존 task도 MAX_TASKS를 사용한다. 앱 worker 256개를 단순 생성했다고 최대치 검증으로 간주하지 않는다. 전용 커널 시험 fixture/최소 구성에서 실제 256개 registry 항목을 구성하고 관찰한 N을 기록해야 함 |
| 누락 tick 반복 | 보드 timer의 catch-up 루프에서 반복 수와 총 ISR 시간, 각 HM 검사/skip/feed 순서. 단일 tick 최댓값만 보고하지 않음 |

ARM PMU cycle counter를 쓸 경우 CPU별 초기화·enable, 실제 주파수, divider, wrap, sleep 중 정지 여부를 먼저 확인한다. 저장소의 `arm_perf.c` 구현이 존재한다는 사실만으로 이미 초기화됐다고 가정하지 않는다. CPU0/1의 서로 다른 counter를 직접 빼지 않는다. sleep 경과는 계속 동작하는 hardware clock이나 외부 측정으로 검증한다.

100ms 같은 새로운 합격 기준이나 검사 후보 수 제한은 도입하지 않는다. 측정값을 실제 tick 간격, 업무 timeout, watchdog 및 wakeup 여유와 함께 검토한다.

메모리는 동일 컴파일러·기준 설정으로 OFF/ON 전체 ELF의 section 크기를 비교한다. `on`과 `test`를 구분해 예제의 크기를 제품 증가량에서 제외한다. GNU `size`의 `text`에는 일반 code 이외의 section도 포함되므로 `size -A`와 map을 같이 보며, `.data + .bss` 정적 RAM, heap/stack 예약, TCB별 동적 증가를 구분한다. TCB 8바이트 증가를 task 256개 전체의 정적 BSS 증가처럼 합산하지 않는다.

## 완료 기록 양식

| 항목 | 결과 | 근거 |
|---|---|---|
| 부팅/첫 feed/무등록 idle | 미실행 | 보드 없음 |
| run/STOP/exit/SMP/PID·TCB 재사용 | 미실행 | 보드 없음 |
| CG/PG sleep, wakeup 여유, sleep 시간 포함 | 미실행 | 보드 없음 |
| 늦은 KICK 순서/복귀 직후 판정 | 미실행 | board gate 준비·실행 필요 |
| PANIC 62 / HW fallback 54 / PANIC 후 HW reset 62 | 미실행 | fault injection·보드 필요 |
| tick/KICK/256후보/잠금/catch-up 시간 | 미측정 | 계측 빌드·보드 필요 |
| OFF/ON/test 전체 빌드와 정적 메모리 | [7단계 결과](07-integration-validation.md) | 펌웨어 산출물 기준 |

실제 보드 항목과 측정 결과를 채운 후 적용한 5000ms 값, PM 여유, 제품 thread별 등록 지점을 검토한다. 제품 thread 일괄 등록은 이번 단계 범위에 포함하지 않는다.
