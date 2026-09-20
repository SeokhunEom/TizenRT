# 7단계: 통합 검증·기준 설정 활성화

2026-09-20 후속: 리뷰 수정 이후의 RTL OFF/ON/test 전체 빌드와 QEMU 최신 실행을 [사전 검증 보고서](../analysis/Health_Monitor_Preboard_Validation.md)에 완료 기록했다. [현재 아키텍처와 구현](architecture-and-implementation.md)을 함께 참고한다. 아래는 7단계 진행 당시 기록이다.

전제: 6단계가 확인·커밋됐고 사용자가 7단계 시작을 지시했다. [공통 진행 방식](README.md)과 [설계 문서](../HealthMonitorImplementationPlan.md)의 11절을 사용한다.

## 목표

앞 단계에서 구현한 기능을 기준 보드에서 함께 확인하고, 사용자가 실제 적용 여부를 판단할 수 있는 결과와 설정 diff를 제시한다.

## 구현 범위

1. 기준 defconfig의 health monitor·HW WDT 활성화 내용을 준비한다. 제품의 기존 주요 스레드에 일괄 등록을 추가하는 작업은 별도 범위로 둔다.
2. 앞 단계에서 마련한 검증 코드를 재사용하고 SMP·절전·리셋 통합 시나리오를 필요한 만큼 보완한다.
3. 앱에서의 최소 호출 예와 최종 설정·사용법, 측정 결과를 문서에 반영한다.
4. 통합 검증 중 발견한 수정이 앞 단계의 설계를 바꾸면 변경 이유와 영향을 사용자에게 제시한다.

예상 변경 위치: 기준 defconfig, 최소 검증 예제 또는 기존 테스트, 사용·검증 문서. 일반적인 코드 정리나 추가 감시 기능을 함께 넣지 않는다.

## 검증

- SMP에서 등록·KICK·STOP·종료를 교차 실행하고 TCB/PID 재사용을 확인한다.
- 정상 KICK, 중단, 검사 전 늦은 KICK, semaphore 대기의 timeout 결과를 확인한다.
- PM wakeup, 실제 sleep 시간 포함, 복귀 직후 KICK 정책을 확인한다.
- SW PANIC·신규 reason과 HW WDT 리셋을 확인한다.
- 평상시 tick, KICK, 최대 256개 후보 동시 도래의 ISR 실행 시간과 잠금 대기시간을 측정한다. 누락 tick 반복 처리도 포함한다.
- 실제 메모리 증가량과 활성·비활성 구성의 빌드를 확인한다.

100ms 합격 기준이나 후보 처리 개수 제한은 도입하지 않는다. 앞 단계에서 확인한 개별 기능은 새로운 변경이나 실패가 없는 한 통합 시나리오에 필요한 범위에서 재사용한다.

## 사용자 확인 항목

- 측정된 부하와 메모리 사용량이 허용 가능한가?
- 앱 호출·절전·리셋의 최종 동작이 의도한 범위와 일치하는가?
- 실제로 확인한 항목과 아직 확인하지 못한 항목이 명확한가?
- 기준 defconfig 활성화 diff를 적용해도 되는가?

완료 기준: 활성화할 설정과 기능·성능·메모리 검증 결과를 제시하고, 사용자가 확인한 범위를 커밋했다. 실제 보드 검증이 남아 있다면 구현 완료와 보드 검증 완료를 구분해 보고한다.

## 2026-09-18 구현·시험 준비

기준 커밋은 6단계 `d1839f070`이다. 사용자가 보드를 사용할 수 없다고 답해 이번 제출 범위를 **검증 앱·활성화안·전체 빌드·메모리 비교·보드 시험 준비**로 정했다. 실제 부팅·절전·리셋과 실행 시간은 미검증이다. 초기 제출에서는 제품 defconfig를 적용하지 않았다. 이후 사용자 지시에 따라 아래의 대표 설정 적용을 완료했고 커밋·origin push 승인을 받았다.

- [검증 예제](../../apps/examples/health_monitor/health_monitor_main.c): 공개 open/ioctl API만 사용한다. run, STOP 후 생존, 등록한 채 worker 종료, CPU0/1에 고정한 공유 fd SMP 반복, semaphore timeout, reboot reason 조회를 제공한다. SMP는 두 worker 생성 후 gate를 열고 각 라운드 종료를 기다린다. 부분 생성 실패 시에도 생성된 worker를 풀고 회수한다.
- [설정 활성화안](rtl8730e-health-monitor.config): HM + tick 소유 WDG4, 명목상 5000ms interval. [설정 도구](configure-validation.py)로 새 복사본에 OFF / ON / 검증 앱 포함 test 설정을 만들고 diff를 출력한다. 기존 제품 thread를 자동 등록하지 않는다.
- [사용·보드 시험 절차](board-validation.md): 최소 API 예, TASH 명령, PM 진입 확인, reason 62/54, PID/TCB 재사용, 늦은 KICK 순서, tick/KICK/잠금/256후보/catch-up 계측 조건을 기록했다. 일부 강제 순서·fault-injection·성능 계측에는 별도 임시 커널 시험 코드가 필요하며, 이번 예제에 제품용 hook을 넣지 않았다.

공개 API, timeout 정책, registry/tick/PM/WDT 구현은 바꾸지 않았다. 초기에는 검토 가능한 fragment로 준비했고, 이후 사용자 지시로 대표 defconfig에 적용했다. 보드 실측 상태는 그대로 미검증이다.

### Linux 호스트 검증

`tizenrt/tizenrt:2.0.1-arm64-rtl8730e-local`, Linux AArch64 GCC 5.4.0, `-Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all`로 기존 19종과 새 예제 UP/SMP 2종을 실행했다.

```sh
make -C os/kernel/health_monitor/tests test OUT_DIR=/results/host-tests
```

21개 실행 파일과 watchdog 설정 경계의 positive/negative compile 검사가 통과했다. reboot reason 명령, 0ms 입력 거부, 보호 모드 `set_errno()` 수정 후에는 예제 2종을 별도로 다시 빌드·실행해 통과했다. PASS 출력에는 예제 fixture가 재사용하는 driver 테스트 출력이 중복되므로 출력 줄 수와 실행 파일 수는 다르다.

새 테스트는 예제 소스 → 실제 VFS/driver → 실제 registry 경로를 사용한다. 숫자/범위/인자 오류, START/KICK/STOP 횟수, EINTR 재대기, STOP 후 원래 deadline 경과, worker 종료 cleanup, 공유 fd SMP 반복, 두 번째 pthread 생성 실패 시 첫 worker 회수, KICK 실패 후 STOP, reason 조회 성공/실패를 확인했다. host pthread 종료에서 호출하는 cleanup, affinity와 시간은 모델이다. 실제 task scheduler, SVC, 보드 sleep/PANIC/reset을 실행한 것은 아니다. ARM 실헤더를 사용한 예제 UP/SMP의 `-Wall -Wextra -Werror` 컴파일도 통과했다. 이 과정에서 호스트가 허용하던 `errno = 0`이 TizenRT 보호 모드에서는 불가함을 발견해 `set_errno(0)`으로 수정했다.

기존 deterministic timer/PM/watchdog fixture는 검사 전 늦은 KICK, 256개 후보 처리, 잠금 실패 시 검사·feed 생략, sleep 시간 반영, PM budget, reason 보존을 계속 검증한다. 이 결과를 target ISR 시간이나 실제 HW 동작의 측정값으로 쓰지 않는다.

### 전체 펌웨어와 메모리

기준은 ARM GCC 10.3.1과 같은 `rtl8730e/loadable_ext_ddr_st7785` 설정이다. OFF/ON 전체 `make -j1`과 test의 `make -j1 JOBS=-j2`가 링크·Realtek 후처리·패키징까지 모두 종료 코드 0으로 완료됐다. 기존 외부 라이브러리 경고를 모두 제거한 빌드는 아니며, 새 예제에는 별도 엄격 컴파일을 적용했다.

| 항목 | OFF | ON (예제 제외) | 차이 |
|---|---:|---:|---:|
| kernel `.xip_image2.text` | 928,188 B | 928,516 B | +328 B |
| kernel RAM `.code` | 12,288 B | 12,288 B | 0 |
| kernel `.data` | 7,364 B | 7,356 B | −8 B |
| kernel `.bss` | 127,800 B | 129,896 B | +2,096 B |
| kernel `.data + .bss` | 135,164 B | 137,252 B | **+2,088 B** |
| common `.data + .bss` | 11,072 B | 11,072 B | 0 |
| app1 `.data + .bss` | 32 B | 32 B | 0 |
| kernel TRPK | 1,679,690 B | 1,680,002 B | +312 B |
| common TRPK | 950,512 B | 950,512 B | 0 |
| app1 TRPK | 449 B | 449 B | 0 |

이 비교는 HM와 WDG4 소유권 전환을 함께 적용한 순증가량이다. 기존 사용자 watchdog 경로가 링크에서 빠지는 효과도 포함하며, HM만 따로 켠 비용으로 해석하지 않는다. `.fini_array`에는 4096 정렬용 padding이 포함되어 1,564→1,236 B로 줄었다. 따라서 code section 변화와 최종 Flash 패키지 변화가 같을 필요는 없다.

실제 linker의 `__psram_heap_buffer_start__`는 `0x60126900`→`0x60127100`으로 이동했다. 해당 링크 영역의 heap 시작 전 점유는 정렬까지 포함해 **2,048 B** 증가했다. `.heap` 8,192 B와 `.stack` 2,048 B 예약 크기는 같다. `.data/.bss` 합계와 실제 heap 경계는 정렬 때문에 서로 다른 지표다.

실제 ARM 헤더로 `sizeof`를 담은 객체를 컴파일하고 `nm -S`로 확인한 동적 TCB 구조체 크기는 다음과 같다.

| 구조체 | OFF | ON | 차이 |
|---|---:|---:|---:|
| `tcb_s` | 232 B | 240 B | +8 B |
| `task_tcb_s` | 252 B | 260 B | +8 B |
| `pthread_tcb_s` | 320 B | 328 B | +8 B |

추가 동적 TCB의 실제 allocator 점유는 할당 단위/metadata의 영향도 받는다. 위 값은 구조체 크기이며 런타임 heap high-water mark는 아니다. 이미 `.bss`에 포함된 idle TCB 증가량을 다시 정적으로 합산하지 않는다. 보드 메모리 실측과 성능 측정은 미실행이다.

검증 앱을 포함한 test 구성은 ON 대비 kernel/app1 크기가 같고, common `.text` +1,300 B, `.rodata` +784 B, `.ARM.exidx` +16 B, `.bss` +32 B였다. common TRPK는 952,608 B로 **예제 포함 시 +2,096 B**다. 이 비용과 예제 실행 시의 동적 worker/stack/fd 비용은 제품 활성화안의 증가량에 포함하지 않았다.

[구성별 산출물·크기·SHA256 기록](07-validation-results.json)에 최종 수치를 보존했다. `arm-none-eabi-size -A`와 `nm -S`를 사용했고, 세 구성 모두 다음을 독립 대조했다.

- kernel/common/app1 ELF32 little-endian ARM 헤더 및 최종 SHA256.
- kernel/common/app1/resource의 TRPK CRC32와 전체 길이, 저장소 파티션 검사 PASS. resource의 별도 4096 B header/padding 형식을 반영했다.
- 8192 B bootparam의 BP1 CRC와 BP2 erased 영역.
- FIP의 BL2/BL32/BL33 포함.
- HM kernel 심볼은 ON/test에 존재하고 OFF에는 부재. `health_monitor_main`은 test의 common에만 존재.

로그·ELF·패키지는 `/tmp/health-monitor-step7.QMkmrx/`에 구성별로 분리했다. OFF 첫 병렬 빌드는 최상위 post 선행 실행으로 실패해 직렬로 다시 실행했다. ON/test 동시 빌드 중 test의 외부 onert-micro C++ 컴파일러가 `Killed signal`로 종료되어 test를 중단했다. Docker의 총 메모리 한도는 약 3.8GiB였고 두 빌드가 대부분을 사용했다. ON 성공 뒤 test를 단독으로 `make -j1 JOBS=-j2`로 재개했으나, 중단 때 남은 빈 onert 객체 7개와 불완전한 external 아카이브 때문에 링크가 실패했다. onert 객체 전체, external 아카이브와 완료 marker를 제거한 뒤 다시 컴파일·아카이브·링크했다. 이 문제를 HM 소스의 컴파일 오류로 분류하지 않는다.

### 남은 확인

- 보드 실측: 부팅/첫 feed, 정상·SMP 수명, TCB/PID 재사용, CG/PG 절전, 복귀 직후 KICK, SW PANIC 62, 순수 HW reset 54, PANIC 후 HW fallback 62.
- 부하: 평상시 tick, KICK, 최대 256개 후보, thread lock 대기, 반복 catch-up의 cycle/시간. 합격 기준이나 후보 수 제한은 새로 정하지 않았다.
- 사용자 검토: 활성화안의 5000ms와 `/dev/watchdog0` 소유권 변경, 실측 후 허용 가능한 메모리/부하/절전 여유. 대표 defconfig 적용은 사용자 지시로 완료했다.

이번 커밋 메시지는 `health_monitor: enable rtl8730e and add validation example`이다. 대표 설정 활성화와 검증 예제 추가를 의미하며 실제 보드 검증 완료를 뜻하지 않는다.

### 독립 리뷰와 제출 상태

`d1839f070` 대비 미커밋 변경을 Standards와 Spec 두 축으로 독립 검토했다. 필수 기준 위반과 명세 결함은 남아 있지 않다. period=0과 도움말의 불일치는 거부 조건·회귀 테스트를 추가해 해결했다. R07 함수 배치·R15 static 접두사는 기존 인접 예제/테스트의 관례를 따른 비차단 권고 차이로 기록했다.

검증 앱·설정안·빌드/메모리 비교·보드 시험 절차 준비가 완료됐다. `rtl8730e/loadable_ext_ddr_st7785`에만 활성화 설정을 적용했으며, 사용자가 커밋·origin push를 승인했다. 보드 실측은 남아 있다.

### 대표 defconfig 적용

사용자가 RTL8730E 대표 한 구성으로 `loadable_ext_ddr_st7785`를 지정해 활성화를 지시했다. 해당 [defconfig](../../build/configs/rtl8730e/loadable_ext_ddr_st7785/defconfig)에 `HEALTH_MONITOR=y`, `WATCHDOG_FOR_IRQ=y`, interval 5000ms와 `ARCH_HAVE_WDOG_WAKEUP=y`를 적용했다. 기존 `WATCHDOG=y`는 유지하며 검증 예제는 명시적으로 OFF다. 다른 defconfig와 Kconfig 기본값은 변경하지 않았다.

검증 도구도 기준 설정의 ON 여부에 관계없이 OFF/ON/test를 만들도록 수정했다. OFF는 기존 watchdog 장치를 유지한 활성화 전 측정 구성, ON은 예제 없는 현재 대표 설정, test는 ON에 검증 앱만 추가한 구성이다.

앞의 [빌드 결과 JSON](07-validation-results.json)은 최초 7단계 빌드 시점의 기록으로 유지한다. 설정 도구와 fragment의 source hash는 적용 전 버전의 값이다. 실제 C 구현은 바뀌지 않았다. 설정 적용 후 실제 `configure.sh`와 검증 도구를 임시 디렉터리에서 실행했다. 기본 configure 결과는 기존 ON 빌드 구성과, 도구의 OFF/ON/test 결과는 각각 기존 빌드 구성과 유효 설정이 같음을 확인했다. 대조 대상은 기준 커밋과 적용 전 도구의 규칙으로 복원하고, JSON에 저장된 설정 SHA256과 먼저 일치시켰다. 덮어쓰기 거부, 감시 관련 설정의 중복 부재, 기존 C/예제/테스트 소스 hash도 확인했다. 이번 적용에서는 전체 펌웨어를 다시 빌드하지 않았다.
