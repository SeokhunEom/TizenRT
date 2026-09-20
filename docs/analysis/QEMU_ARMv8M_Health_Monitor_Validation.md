# Health Monitor ARMv8-M QEMU 검증 보고서

2026-09-20 추가 검증. **실제 사용자 CPU fault → 진단 로그 → fault sender → 오류 메시지 수신까지 20/20 PASS**다. 사용자 요청에 따라 Binary Manager 복구·재로딩과 deadline보다 오래 지속되는 unload 시험은 현재 검증 범위에서 제외했다. 모든 추가 실행은 복구 함수 호출 명령 직전에 중단했다. 아래에는 앞선 Health Monitor API·만료·등록 정리 및 네 recipe 회귀 결과도 보존한다. 이전에 재현한 loader 누수와 app1/TASH 수명주기 실패는 과거 진단 기록이며 이번 메시지 전달 시험의 실패로 집계하지 않는다. 추가 검증 종료 당시 Health Monitor 및 시험 변경은 미커밋이었으며, 검증 단계에서는 stage·commit·push를 하지 않았다. 이 보고서와 증거의 HEAD·index·커밋 수는 각 검증 종료 시점의 스냅샷이다.

## 기준선과 수정 범위

- QEMU 작업 폴더: `/Volumes/T7/Dev/TizenRT/codex/qemu-armv8m-kernel-tc`, 브랜치 `codex/qemu-armv8m-kernel-tc`, 1차 검증 HEAD `b5eacbdc0426df9ca0784dc5198ac8ff30443631`, 추가 메시지 검증 시작/종료 HEAD `5c0120f685ff47ab17f7ec26c0554965e312bc27`.
- Health Monitor 작업 폴더: `/Volumes/T7/Dev/TizenRT/codex/260901-health-monitor`, 브랜치 `codex/260901-health-monitor`, 시작/종료 HEAD `eddceb9665e302a32e78742359669ed4ee1c8e79`.
- 1차 검증 시작 시 두 폴더는 clean이었다. 이후 사용자 요청으로 QEMU의 독립적인 tick 수정 `82ed01c10`과 UART 수정 `5c0120f68`만 커밋했다. 추가 검증 종료 당시 나머지 Health Monitor·시험·문서 변경은 미커밋이었으며 두 index는 비어 있었다. 기존 증거 폴더의 HEAD/index/커밋 수는 1차 검증 시점의 기록이다.
- 기존 Health Monitor 구현과 scheduler/TCB 정리 hook, 드라이버, QEMU 보드 초기화를 통합했다. 오래된 QEMU `reboot_reason.h`에는 기존 번호를 유지하며 reason 62만 추가했다.
- QEMU 포트에서 TIMER0 tick 누락 보정과 TCSETS 수신 인터럽트 보존을 수정했다. 발견한 범용 ELF/앱 수명주기 문제는 별도 결과로 기록했고 해당 로더/메모리 코드를 수정하지 않았다.
- Health Monitor 브랜치에는 flat 시험 일반화, protected 사용자 fixture/시험 ioctl, 실행기와 이 보고서·증거를 작성했다.

## 환경과 최종 설정

MPS2-AN505 / Cortex-M33 / 단일 CPU / 20MHz / 1ms tick / TIMER0 IRQ 19. QEMU 11.1.1, Homebrew Python 3.10 이상, Homebrew Bash를 사용했다. 빌드는 `os/dbuild.sh`의 clean/reconfigure/build 메뉴와 같은 명령 경로를 사용했다. ARM64 image는 `tizenrt/tizenrt:2.0.1-arm64-local`, image ID는 `sha256:16b314997d3ed7901fe711ff85e1cd8e90885f44bb3d683fc7040699ebe2b74c`다.

최종 네 recipe는 Health Monitor와 일반 예제만 켠 상태다. 시간/PID 주입 fixture와 protected 제어 fixture를 껐고, 원래 task 수·파일시스템·재로딩 정책을 복원했다. protected 재로딩 시험 동안만 `BINMGR_RECOVERY=y`, `BINMGR_RELOAD_REBOOT=n`을 사용했다. 최종 recipe는 원래 reboot 정책이다.

| Recipe | 구조 | MAX_TASKS / PI | Network | Kernel | 증거 |
|---|---|---|---|---|---|
| hello | flat | 32 / off | 161 / 0 | 459 / 0 | [빌드](evidence/health-monitor-armv8m-20260920/hello-final-build/result.json), [회귀](evidence/health-monitor-armv8m-20260920/hello-final-regression.json) |
| loadable_all | protected, RAM ELF | 32 / off | 161 / 0 | 447 / 0 | [빌드](evidence/health-monitor-armv8m-20260920/loadable_all-final-build/result.json), [회귀](evidence/health-monitor-armv8m-20260920/loadable_all-final-regression.json) |
| loadable_apps | protected, kernel XIP | 32 / off | 161 / 0 | 447 / 0 | [빌드](evidence/health-monitor-armv8m-20260920/loadable_apps-final-build/result.json), [회귀](evidence/health-monitor-armv8m-20260920/loadable_apps-final-regression.json) |
| xip_all | kernel/common/app XIP | 256 / on | 161 / 0 | 447 / 0 | [빌드](evidence/health-monitor-armv8m-20260920/xip_all-final-build/result.json), [회귀](evidence/health-monitor-armv8m-20260920/xip_all-final-regression.json) |

네트워크 집계는 포트의 공식 runner 기준이다. DHCP, DNS, 가상 IPv4/IPv6 gateway와 network TC를 확인했다. 외부 공용 ICMP는 0/1 응답인 기록도 있으며, 공식 runner의 비필수 관찰 항목이다. 실제 Wi-Fi나 외부 네트워크 전체 정상성의 증거가 아니다.

[최종 설정/진단 문자열 제외 확인](evidence/health-monitor-armv8m-20260920/production-fixture-exclusion.json), [빌드별 전체 firmware hash](evidence/health-monitor-armv8m-20260920/firmware-manifest.json), [최종 소스·HEAD·index](evidence/health-monitor-armv8m-20260920/final-source-state.json).

## Flat 실제 커널 시험

| 항목 | 실행과 결과 | 증거 |
|---|---|---|
| API·errno·timeout 변환 | 1/9/10/11ms, INT32_MAX, INT32_MAX+1, UINT32_MAX, 중복 등록·무등록 KICK/STOP, 잘못된 fd/ioctl, read/write, 실패 시 무변경 | [결과](evidence/health-monitor-armv8m-20260920/hello-stress256-all/result.json) |
| 공유 fd와 thread 소유권 | 같은 fd를 쓰는 실제 pthread의 독립 START/KICK/STOP, close 후 등록 유지 | 위 결과의 shared/API |
| 수명주기 | pthread return/exit/cancel, task delete/restart, 종료 뒤 옛 deadline 경과, 등록·힌트 제거 | 위 결과의 lifecycle |
| PID/TCB 재사용 | allocator cursor를 시험용으로 조정. 실제 PID 93회·TCB 주소 93회 재사용, 새 등록 오염 없음 | 위 결과의 reuse |
| 정상 시간 부하 | 파일 I/O와 KICK 병행, 210,000 START/KICK/STOP cycles | 위 결과의 active/stress |
| 다수 등록 | 실제 worker 128개 동시 등록·등록 상태 종료 × 3회, 옛 deadline 후 생존 | 위 결과의 many |
| 반복 안정성 | 8개 정상 시나리오 × 3회, 10,767 CHECK, 완료 후 task 집합 동일, heap 증가 0 bytes | 위 결과 |
| 정적 용량 | capacity=4 × 3회, 각 53 CHECK. ENOSPC 무변경과 슬롯 재사용 | [결과](evidence/health-monitor-armv8m-20260920/hello-capacity4/result.json) |
| fatal 경계 | close/equality/overdue/late/wrap/stale/multi/equal/far/unstable 10종 모두 의도한 단일 HM PANIC·IRQ19 | [실행 목록](evidence/health-monitor-armv8m-20260920/run-summary.json) |

fatal의 PID·실제 검사 now·deadline과 PANIC 위치를 대조했다. controlled time과 PID cursor 조정은 flat 시험에만 있으며 자연 시간·실제 SMP 경합으로 해석하지 않는다.


## 타이머와 콘솔 수정의 재현/회귀

1. 수정 전 HM on의 1000ms 만료는 1.505초가 걸렸다. HM off에서도 1000 ticks에 1.271초가 걸렸다. TIMER0 RELOAD=20000, pclk=20MHz는 실제 레지스터와 qtree에서 확인했다. TIMER1 기준으로 OS tick이 늦었으며, 같은 기존 ELF의 icount 실행에서는 그 비율이 약 1 ticks/ms로 바뀌었다.
2. TIMER0의 단일 pending bit는 지연된 여러 만료를 구분하지 못한다. 이제 전용 TIMER1을 32-bit free-running counter로 두고 실제 경과 counts를 tick으로 환산한다. 나머지를 보존하며 unsigned subtraction으로 counter wrap을 처리한다. TIMER1은 약 214.75초의 한 전체 주기보다 자주 읽혀야 한다.
3. 수정 후 자연 1000/2000ms 만료는 각각 1.001/2.002초였다. 정확한 HM PANIC과 IRQ19, `-no-reboot` QEMU의 정상 종료로 software reset을 확인했다. 물리 watchdog reset/reboot reason 보존 증거는 아니다.
4. TCSETS가 UART 전체 초기화를 호출하면서 RX interrupt enable을 지웠다. 최소 재현은 TCGETS/TCSETS 14개 검사까지 통과한 뒤 `ps`가 31.371초 동안 응답하지 않는 것이었다. baud 설정만 갱신하도록 분리한 뒤 같은 재현과 TASH 후속 명령이 통과했다.

- 수정 전: [결과](evidence/health-monitor-armv8m-20260920/hello-reset-1000-isolated/result.json), [결과](evidence/health-monitor-armv8m-20260920/hello-hm-off-clock-unrestricted/result.json), [결과](evidence/health-monitor-armv8m-20260920/hello-serial-red/result.json).
- 수정 후: [결과](evidence/health-monitor-armv8m-20260920/hello-port-fix-reset-1000/result.json), [결과](evidence/health-monitor-armv8m-20260920/hello-port-fix-reset-2000/result.json), [결과](evidence/health-monitor-armv8m-20260920/hello-port-fix-serial-1000/result.json).

- 최종 probe-off ELF의 실제 TIMER1 순환 시험: 220058 ticks / 220.058167 reference seconds, wrap 확인, PASS. [결과](evidence/health-monitor-armv8m-20260920/hello-final-clock-wrap/result.json).
- 처음 실패한 전체 normal 재현도 최종 probe-off ELF로 통과했다. 정상/STOP/300 worker 종료 → kernel 459/0 → TASH와 Health Monitor 재사용 → task/heap 재확인까지 성공했다. [결과](evidence/health-monitor-armv8m-20260920/hello-final-normal/result.json).

QEMU 모델 참고: [AN505 클록](https://github.com/qemu/qemu/blob/master/hw/arm/mps2-tz.c), [TIMER0/1 연결](https://github.com/qemu/qemu/blob/master/hw/arm/armsse.c), [CMSDK timer](https://github.com/qemu/qemu/blob/master/hw/timer/cmsdk-apb-timer.c). 설치 버전의 값은 보관한 qtree/레지스터 관찰로 별도 확인했다.

## Protected / loadable / XIP 1차 시험

아래 재로딩/종료 결과는 범위 조정 전의 진단 기록이다. 이번 추가 검증은 다음 절의 CPU fault 메시지 전달만 대상으로 하며, 복구와 unload 지연은 판정 대상에서 제외한다.

사용자 앱은 공개 open/ioctl/pthread API만 호출한다. CONTROL=3, unprivileged=1을 실제 앱에서 읽었다. TASH 제어 클라이언트는 기존 OS API test driver의 시험용 ioctl을 사용하고, 커널 관찰기만 TCB와 다음 검사 힌트를 읽는다. recovery 요청은 syscall 반환 뒤 커널 LPWORK에서 실행해 요청한 앱과 TASH를 안전하게 제거할 수 있게 했다.

| 구성 | 공개 API 반복 | 등록 중 main 종료 | 1000ms 만료 | 실제 재로딩 × 3회 |
|---|---|---|---|---|
| loadable_all | app1/app2 각각 3회 PASS | app2 PASS, app1 FAIL | 0.994초 PASS | 등록/PID 정리 PASS, heap 증가로 전체 FAIL |
| loadable_apps | app1/app2 각각 3회 PASS | app2 PASS, app1 FAIL | 0.994초 PASS | 등록/PID 정리 PASS, heap 증가로 전체 FAIL |
| xip_all | app1 3회 PASS | app1 FAIL | 0.998초 PASS | 등록/PID 정리 PASS, heap 증가로 전체 FAIL |

각 API 회차는 앱당 3,055 CHECK와 1,000 START/KICK/STOP cycles를 포함한다. shared fd caller identity, errno, 32-bit 인자 경계, close/reopen, pthread cancel/exit/return을 검사했다. 회차 간 등록/힌트는 0이고 task/heap 증가는 없었다. xip_all에는 app2가 없다.

실제 reload에서는 app1 main+자식 4개를 등록한 뒤 Binary Manager의 deactivate/unload/load를 실행했다. 매회 등록 5→0, hint 1→0, 옛 PID 소멸, 새 앱 readiness, 옛 5초 deadline 이후 생존, system reboot 없음과 task 수 복귀를 확인했다. 이 시험은 recovery 진입점을 제어된 명령으로 호출한 것이며, 이 1차 시험만으로 실제 사용자 CPU fault의 fault-message 전달 경로를 증명하지 않는다. 해당 메시지 수신까지의 경로는 아래 추가 검증으로 별도 확인했다.

증거: [RAM API](evidence/health-monitor-armv8m-20260920/loadable_all-user-api-retry/result.json), [kernel XIP API](evidence/health-monitor-armv8m-20260920/loadable_apps-user-api/result.json), [전체 XIP API](evidence/health-monitor-armv8m-20260920/xip_all-user-api/result.json), [RAM reload](evidence/health-monitor-armv8m-20260920/loadable_all-user-reload/result.json), [kernel XIP reload](evidence/health-monitor-armv8m-20260920/loadable_apps-user-reload/result.json), [전체 XIP reload](evidence/health-monitor-armv8m-20260920/xip_all-user-reload/result.json).

## 1차 시험에서 별도로 발견한 실패와 대조군

### 반복 reload 메모리 누수

RAM ELF 두 구성은 회당 **26,112 bytes**, 전체 XIP는 회당 **256 bytes** 증가했다. 모니터 등록이 없는 대조군에서도 같은 증가가 발생했다. task 수와 HM 등록은 정상으로 복귀했지만 전체 메모리 안정성은 FAIL이다.

실제 `heapinfo -k -a` allocation owner를 해당 ELF의 System.map으로 대조한 1회 증가량:

| 잔존 할당 | RAM ELF | 전체 XIP | 소스상의 설명 |
|---|---:|---:|---|
| common export 해시 table/object | 25,856 + 32 | 해당 없음 | `libelf_bind.c`의 `export_library_symtab()`이 이전 `g_lib_symhash`를 해제하지 않고 교체 |
| common binary metadata | 160 | 192 | common unload가 `unload_module()` 뒤 `g_lib_binp`를 NULL로 바꾸지만 metadata 객체를 해제하지 않음 |
| 종료된 TASH의 select pollset | 64 | 64 | 강제 unload가 대기 중 syscall의 정상 반환/해제를 지나지 않음 |

이 로더/VFS 파일은 이번 작업에서 수정하지 않았다. 대조군은 HM 기능을 완전히 끈 빌드가 아니라 **reload 시 활성 등록이 0인 빌드**다. 잔존 allocation owner와 변경되지 않은 소스를 함께 근거로 HM static registry 누수와 구분했다.

대조군/원시 heap 기록: [결과](evidence/health-monitor-armv8m-20260920/loadable_all-user-reload-empty/result.json), [결과](evidence/health-monitor-armv8m-20260920/xip_all-user-reload-empty/result.json).

### app1 main 종료 후 자식 TASH 접근

app1은 preapp에서 TASH 자식을 시작한다. main return의 `binfmt_exit()`가 앱 코드/힙을 해제한 뒤 남은 TASH 또는 그 자식이 실행하면서 MPU IACCVIOL이 발생했다. 등록 없는 `return-empty` clean-build 대조군에서도 재현됐다. 자식이 없는 app2 main은 등록한 채 종료해도 PID/등록/힌트가 제거되고 옛 deadline 뒤 정상이다.

등록 비교: [결과](evidence/health-monitor-armv8m-20260920/loadable_all-user-return/result.json), [결과](evidence/health-monitor-armv8m-20260920/loadable_all-user-return-empty-clean/result.json), [결과](evidence/health-monitor-armv8m-20260920/loadable_all-user-return-app2/result.json), [결과](evidence/health-monitor-armv8m-20260920/xip_all-user-return/result.json), [결과](evidence/health-monitor-armv8m-20260920/xip_all-user-return-empty/result.json).

1차 시험에서 main보다 오래 사는 자식을 둔 app1의 정상 종료와 reboot 없는 반복 recovery는 실패했다. 해당 문제를 수정한 것으로 주장하지 않는다. 이 과거 진단 결과를 보존하되, 사용자가 지정한 이번 메시지 전달 검증의 완료 조건에 복구·unload 성공을 요구하지 않는다.

## 실제 사용자 CPU fault와 오류 메시지 전달 추가 검증

[전체 20개 결과](evidence/health-monitor-armv8m-fault-message-20260920/matrix-summary.json), [검증·보존 확인](evidence/health-monitor-armv8m-fault-message-20260920/verification.json). 각 결과 옆에 원본 serial 로그와 GDB RSP 기록을 압축해 보관했다.

| 구성 | 앱 | fault × HM 등록 유무 | 결과 |
|---|---|---|---|
| loadable_all | app1 / app2 | UDF, MPU read × 등록 / 미등록 | 8/8 PASS |
| loadable_apps | app1 / app2 | UDF, MPU read × 등록 / 미등록 | 8/8 PASS |
| xip_all | app1 | UDF, MPU read × 등록 / 미등록 | 4/4 PASS |

사용자 main thread가 직접 `udf #0`을 실행하거나 커널 RAM `0x80000000`을 읽었다. 함수 호출로 assert를 흉내 내거나 PC/메모리를 디버거로 변조하지 않았다. 실제 앱에서 CONTROL=3을 확인하고, fault 진입 시 TCB의 PID·binary ID와 HM timeout을 관찰했다. 등록군은 공개 ioctl로 60,000ms를 등록했고 대조군은 timeout=0이었다. 단일 코어 1ms tick 기준이다.

- **UDF:** 이 포트의 초기화는 UsageFault를 enable하지 않는다. 실제 예외는 IRQ3 HardFault로 승격되고 CFSR=`0x00010000`(UNDEFINSTR), HFSR=`0x40000000`(FORCED), fault PC의 실제 명령 bytes=`00de`가 확인됐다. 진단 로그의 PC도 일치했다.
- **MPU read:** IRQ4 MemManage, CFSR=`0x82`(DACCVIOL + MMARVALID), MMFAR=`0x80000000`, `FAULT TYPE: DACCVIOL` 진단과 fault PC 일치를 확인했다.
- **실제 송수신:** fault handler → `binary_manager_recover_userfault()`의 메시지 발행 경로 → 기존 fault sender → `mq_send()` → Binary Manager의 `mq_receive()`가 실행됐다. sender와 receiver는 서로 다른 실제 커널 thread였다. `BINMGR_FAULT=10`, binary ID(app1=1/app2=2), priority=100, 길이=24 및 송수신 전체 24바이트 일치를 확인했다. `requester_pid` 필드는 이 명령에서 thread PID가 아니라 binary ID를 담는다. PC/레지스터 덤프는 serial 진단이며 MQ payload에 포함되는 것으로 주장하지 않는다.
- **복구 제외:** 실제 ELF disassembly에서 찾은 `binary_manager_recovery` 호출 명령에 hardware breakpoint를 설정했다. 메시지 수신 후 그 명령을 실행하기 전에 QEMU를 종료했다. 복구 함수 본문·loader/unload/reload·등록 정리 성공 여부는 이번 시험에서 실행하거나 판정하지 않았다. fault 분류 과정의 RT thread 비활성화와 sender 깨우기는 메시지 발행 경로에 포함된다.

메시지 발행/수신 코드 자체가 `CONFIG_BINMGR_RECOVERY` 조건 안에 있으므로 시험 빌드에서는 이 설정을 켜고 `BINMGR_RELOAD_REBOOT`를 껐다. 이는 복구 시험을 포함한다는 뜻이 아니다. 사용자 코드에는 두 fault trigger만 추가했으며 기존 fault handler와 Binary Manager 제품 소스는 수정하지 않았다. 디버거는 읽기와 hardware breakpoint만 사용했다. metadata와 실제 ELF/config SHA-256 일치도 실행 전에 검사했다.

대표 증거: [app1 UDF](evidence/health-monitor-armv8m-fault-message-20260920/matrix-loadable_all-app1-udf-registered/result.json), [app2 MPU](evidence/health-monitor-armv8m-fault-message-20260920/matrix-loadable_apps-app2-mpu-registered/result.json), [XIP MPU 미등록 대조군](evidence/health-monitor-armv8m-fault-message-20260920/matrix-xip_all-app1-mpu-unregistered/result.json).

최초 UDF 탐색 실행은 전용 UsageFault handler만 예상한 시험 판정 때문에 실패했다. 실제로는 HardFault로 승격되어 sender까지 도달한 상태였고 복구는 실행되지 않았다. 실제 예외 종류를 반영한 뒤 위 20개 조합이 모두 통과했다. 그 탐색 기록도 별도 보존했다. 추가 실행 후 네 defconfig를 이전 내용으로 복원하고, 원래 hello 설정을 다시 빌드하여 fault fixture가 최종 펌웨어에서 제외됨을 확인했다.

재현 도구: [설명](../../tools/qemu-armv8m-health-monitor/README.md), [ELF metadata 추출](../../tools/qemu-armv8m-health-monitor/fault-debug-metadata.py), [메시지 전용 실행기](../../tools/qemu-armv8m-health-monitor/fault-message.py). 전체 상태 이미지와 ELF는 보고서의 대형 파일 대신 로컬 원본 `/private/tmp/hm-armv8m-fault-message-whudzy7n`에 보존했다. 이 경로는 임시 저장소이며 Git 증거에는 로그·결과·설정·소스 패치·hash를 보관한다.

## 호스트/정적 검증과 증거 보관

- 통합한 QEMU 소스의 Linux ARM64 host executable 21개를 ASan/UBSan으로 실행해 통과했다. 모델 기반 SMP·PM·watchdog 검사는 실제 QEMU/보드 실행과 구분한다. [결과](evidence/health-monitor-armv8m-20260920/host-tests-retry/result.json).
- Python 문법, `git diff --check`, 최종 설정과 fixture 문자열 제외를 확인했다. native syscall/hint assembly 관찰은 별도 [정적 증거](evidence/health-monitor-armv8m-20260920/static-inspection/result.json)에 기록했다.
- [전체 실행 요약](evidence/health-monitor-armv8m-20260920/run-summary.json), [정확한 QEMU 소스 patch](evidence/health-monitor-armv8m-20260920/qemu-worktree.patch), [최종 소스 hash/HEAD/index](evidence/health-monitor-armv8m-20260920/final-source-state.json), [firmware hash](evidence/health-monitor-armv8m-20260920/firmware-manifest.json).
- 저장소 증거에는 JSON·유효 설정·빌드/source manifest와 gzip 직렬/빌드 로그를 보관했다. 큰 ELF/A-B state 파일 원본은 `/private/tmp/hm-armv8m-20260920-n2j8ynnz`에 있다. HEAD만으로 미커밋 빌드를 식별하지 말고 manifest/patch/firmware hash를 함께 사용한다.
- 1차 patch는 당시 QEMU 작업 폴더에서 `git apply --reverse --check`로 확인한 스냅샷이다. 추가 시험의 [최신 QEMU patch](evidence/health-monitor-armv8m-fault-message-20260920/qemu-worktree.patch)는 HEAD `5c0120f68` 기준이며 추가 시험 종료 시점에서 별도 확인했다. 이번 검증에서는 patch 적용/역적용이나 커밋을 수행하지 않았다.

## 재현과 실패 시도 처리

[실행기 사용법](../../tools/qemu-armv8m-health-monitor/README.md)의 설정과 명령을 사용한다. 보관된 recipe별 `effective.config`의 전체 값을 사용해야 한다. configure 단계가 생략한 Kconfig 기본값을 자동 보완하지 않으므로 TMPFS에는 크기·heap index가 모두 필요하다.

- 오래된 QEMU assert-reason helper는 인자를 받지 않는다. 최초 host 시험의 컴파일 오류는 시험 전용 legacy 분기로 보완했다. 제품 helper는 수정하지 않았다.
- 초기 Docker 종료, macOS Python 3.9 비호환, 샌드박스 DNS/로컬 GDB bind 제한은 복구/올바른 실행 환경에서 재시도했다.
- 기존 타이머 레지스터 loader 초기화 실패와 실행 중 첫 GDB attach timeout은 무효 진단이다. 성공한 `gdb-start`/최종 clock 결과와 구분한다.
- hello 기본 recipe의 TMPFS 부재로 파일 부하가 ENOENT였고, 시험 구성에 TMPFS 전체 설정을 넣은 뒤 성공했다.
- 최초 protected fixture는 preapp/TASH 시작이 빠져 boot API는 통과해도 전체 시험이 timeout이었다. 기존 preapp을 유지하고 private 관찰을 kernel test ioctl로 옮긴 뒤 반복 시험을 통과했다.
- 사용자 `.inc` 수정이 incremental app build에 반영되지 않은 `return-empty` 시도는 무효 대조군이다. clean build로 재현한 결과만 결론에 사용했다. 실패 시도도 원본 기록을 보존했다.

## 확인하지 못한 경계

실제 SMP 실행/캐시 순서와 경쟁, 실제 64-bit 타깃, 실제 PM clock gating·wakeup, RTL8730E 하드웨어 watchdog/reset 및 reboot reason 보존, 물리 보드 ISR 최악 지연, 원격 CI는 검증하지 않았다. 실제 사용자 CPU fault에서 오류 메시지 수신까지는 위 추가 시험으로 검증했다. Binary Manager 복구·재로딩 및 deadline보다 오래 지연되는 unload 중간 상태는 사용자 요청에 따른 **검증 범위 제외**이며 미완료 필수 항목으로 두지 않는다. QEMU reset·host model·controlled-time 결과를 이러한 증거로 확장하지 않는다.
