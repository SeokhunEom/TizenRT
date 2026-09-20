# Health Monitor 최신 코드 사전 검증

2026-09-20. **이번 사전 검증을 완료했다.** 최신 제품 코드에서 RTL8730E 전체 빌드 3개, QEMU ARMv8-M 전체 빌드 10개와 실행 시나리오 53개가 통과했다. 실제 사용자 CPU fault → 오류 메시지 수신 20건과 제품 timeout 로그 21건을 포함한다. 유효한 최종 시나리오에 미해결 실패는 없다. 물리 RTL8730E 실행과 사용자가 제외한 Binary Manager 복구·unload 지연은 이 완료 범위에 포함하지 않는다.

[기계 판독 요약](evidence/health-monitor-preboard-20260920/summary.json), [QEMU 전체 실행 목록](evidence/health-monitor-preboard-20260920/qemu-matrix.json), [증거·재현 안내](evidence/health-monitor-preboard-20260920/README.md), [호출 흐름·아키텍처 문서](../health-monitor/architecture-and-implementation.md).

## 기준 코드와 환경

| 항목 | 기준 |
|---|---|
| Health Monitor | `codex/260901-health-monitor`, `6d4deed26f836378d961561c50617cde1a8ce00a` |
| QEMU 포트 | `codex/qemu-armv8m-kernel-tc`, `5c0120f685ff47ab17f7ec26c0554965e312bc27` + 기존 미커밋 통합 + 최신 HM 파일 |
| 격리 작업 경로 | `/private/tmp/hm-preboard-20260920-8_tdqiqr` |
| RTL 대상 | `rtl8730e/loadable_ext_ddr_st7785`, OFF / ON / test |
| QEMU 대상 | `mps2-an505` ARMv8-M UP, hello / loadable_all / loadable_apps / xip_all |
| 도구 | QEMU 11.1.1, Homebrew Bash 5.3.20, Python 3.10 이상 |
| RTL Docker 이미지 | `tizenrt/tizenrt:2.0.1-arm64-rtl8730e-local`, `sha256:aa453f25ebf1648620f34af905d0891e8899efb1e62046d6e6806238426c9a5b` |
| QEMU Docker 이미지 | `tizenrt/tizenrt:2.0.1-arm64-local`, `sha256:16b314997d3ed7901fe711ff85e1cd8e90885f44bb3d683fc7040699ebe2b74c` |

RTL은 기준 HEAD의 `git archive`에서 새 소스 3개를 만들어 각각 빌드했다. QEMU는 별도 checkout에 기존 미커밋 통합을 복제하고 최신 HM 파일 74개를 반영했다. 이후 10개 설정을 순차 clean/reconfigure/build했다. 이전 검증의 **설정 파일만** 재사용했으며 펌웨어는 모두 새로 생성했다.

QEMU의 `source_head=5c0120f68`만으로 실행 코드를 식별할 수 없다. [통합 patch](evidence/health-monitor-preboard-20260920/qemu-latest-overlay.patch), [최신 HM 파일 해시](evidence/health-monitor-preboard-20260920/qemu-health-source-manifest.json), ELF/config SHA-256을 함께 사용한다. 실행 53건의 ELF/config를 새 빌드와 대조했고, 최신 파일 74개는 Health Monitor 원본과 QEMU 복사본에서 동일했다.

이번 검증 중 제품/시험 소스는 변경하지 않았다. 원본 QEMU의 HEAD, `git status`, 미커밋/미추적 파일 87개 해시는 시작과 끝이 같다. 문서와 증거만 Health Monitor 브랜치에 추가한다. [초기 계획](evidence/health-monitor-preboard-20260920/plan.json)의 commit/push 금지는 검증 시작 당시 지시이며, 이후 사용자가 문서 작성과 commit/push를 요청했다.

## RTL8730E 전체 빌드·산출물

| 구성 | HM / IRQ WDT | HM 예제 | 전체 빌드 | 독립 산출물 검사 |
|---|---|---|---|---|
| OFF | OFF | OFF | PASS | PASS |
| ON | ON | OFF | PASS | PASS |
| test | ON | ON | PASS | PASS |

각 빌드는 약 197초였다. 기존 외부 소스 warning은 로그에 보존했으며 compile/link 오류는 없었다. 새 산출물에서 다음을 확인했다.

- ARM ELF32 little-endian machine 40, HM core/제품 로그의 설정 gating, test에서만 예제 포함, PM 누적 시간 콜백 링크.
- kernel/common/app1/resource TRPK의 CRC32, header/payload 길이, resource 4096B header 영역, 두 flash의 실제 partition 범위.
- 8192B boot parameter의 CRC, format/version, 활성 kernel 주소와 설정 일치, update reason, 비활성 4096B 영역 erase 값.
- FIP의 BL2/BL32/BL33 구성, 추출 BL33와 kernel RAM image 해시 일치, 빌드 자체의 partition/size 검사.
- 실제 ELF의 DWARF를 GDB로 읽은 TCB 크기와 ELF section별 크기.

| 항목 | OFF | ON | test |
|---|---:|---:|---:|
| 기본 TCB | 232B | 240B | 240B |
| task TCB | 252B | 260B | 260B |
| pthread TCB | 320B | 328B | 328B |
| Kernel `.data + .bss` | 135,196B | 137,292B | 137,292B |
| Kernel TRPK | 1,680,042B | 1,680,458B | 1,680,458B |
| Common TRPK | 950,512B | 950,512B | 952,608B |
| App1 TRPK | 449B | 449B | 449B |
| Resource TRPK | 40,960B | 40,960B | 40,960B |

ON−OFF kernel 정적 RAM은 **2,096B**, TCB 계열은 **각각 8B** 증가했다. OFF/ON은 HM과 IRQ WDT 활성화 조합의 비교다. `.xip_image2.text`는 432B 증가하지만 다른 section 배치/정렬도 바뀌므로 이를 최종 ROM 비용으로 해석하지 않는다. 최종 kernel TRPK 차이는 416B다. 동적 TCB당 8B 증가는 정적 RAM 차이와 별도다.

[빌드 matrix](evidence/health-monitor-preboard-20260920/rtl-matrix.json), [메모리 측정](evidence/health-monitor-preboard-20260920/rtl-memory.json), [OFF 검사](evidence/health-monitor-preboard-20260920/rtl-off/validation.json), [ON 검사](evidence/health-monitor-preboard-20260920/rtl-on/validation.json), [test 검사](evidence/health-monitor-preboard-20260920/rtl-test/validation.json).

## QEMU 새 빌드 10개와 실행 53개

빌드 구성은 `hello-off`, `hello-production`, `hello-probes`, `hello-capacity4`, `loadable_all-production`, `loadable_apps-production`, `xip_all-production`, `loadable_all-fault`, `loadable_apps-fault`, `xip_all-fault`이며 모두 PASS다.

production은 내부 probe를 끄고 일반 HM 예제로 제품 경로를 실행한 구성이다. RTL 출하 펌웨어라는 뜻은 아니다. probes는 MAX_TASKS=256/TMPFS, capacity는 등록 용량4, fault는 protected 사용자 fixture를 사용했다. 각 새 ELF에서 HM/log gating, `g_health_heap` 크기 = capacity × 8B, ELF/config 해시를 확인했다. [독립 산출물 대조](evidence/health-monitor-preboard-20260920/qemu-artifact-validation.json).

집계 단위는 실행기 1회의 `result.json`이다. 내부 round/조건 검사/kernel test 수는 별도로 표기한다.

| 시나리오 | 실행 수 | 확인 내용 | 결과 |
|---|---:|---|---|
| HM OFF kernel_tc | 1 | 459건 통과, 비활성 구성 회귀 | PASS |
| production normal 4 profiles | 4 | 입력 거부, START/KICK/STOP, 각각 worker 300회 종료, kernel_tc, 종료 후 HM 재실행 | PASS |
| hello 자동 리셋 | 2 | 자연 시간 1000/2000ms 만료, IRQ 19, 정확한 HM PANIC, QEMU software reset | PASS |
| protected production 만료 | 6 | 3 profiles × 1000/2000ms, SYSTEM_HALT 정책 | PASS |
| flat probes all | 1 | 3 rounds, API/shared/lifecycle/reuse/boundary/active/stress/many | PASS |
| flat fatal 경계 | 10 | close/equality/overdue/late/wrap/stale/multi/equal/far/unstable | PASS |
| capacity 4 | 1 | ENOSPC 시 상태 유지, slot 재사용 | PASS |
| protected user API | 3 | 각각 3 rounds, unprivileged 호출/SVC | PASS |
| protected user expiry | 3 | user thread 만료의 kernel 판정과 대상 PID | PASS |
| app2 정상 return | 2 | loadable_all/loadable_apps, 등록된 main 정상 종료 정리 | PASS |
| 실제 사용자 CPU fault → MQ | 20 | 복구 호출 직전 중단 | PASS |
| **합계** | **53** | **모두 새 ELF에서 실행** | **PASS** |

Kernel 회귀는 `459 × 2 + 447 × 3 = 2,259 PASS / 0 FAIL`이다. normal 4개 구성은 각각 worker 300회 종료 뒤 heap 증가 0B였다. 일반 경로에서 PID 재사용을 관측했다고 주장하지 않는다. PID cursor를 통제한 별도 probe에서 PID/TCB 재사용을 확인했다.

Flat probe는 **조건 검사 10,767개**, stress 동작 **210,000회**를 통과했다. `many`는 round마다 실제 worker 128개가 모두 등록한 채 종료한 뒤 이전 deadline을 지나도 생존함을 확인했다. `lifecycle`은 실제 pthread_exit/cancel/task_restart/task_delete/task_return/같은 TCB 재시작을 실행했다. warmup 이후 heap 증가는 0B였다. 일부 경계는 test-only 합성 시각/예약과 PID cursor를 제어하며 실제 system tick IRQ의 판정도 확인한다. 자연 시간 또는 실제 SMP 성능 측정으로 해석하지 않는다.

제품 timeout PID/now/deadline 로그는 **21건** 대조했다. fatal 10건은 fixture의 별도 판정과 같았고, user expiry 3건은 user notice의 PID와 같았다. 나머지는 자연 시간 만료 8건이다. 모두 wrap을 고려한 `now >= deadline` 조건을 만족했다. hello의 1/2초 만료는 호스트에서 1.002/2.000초로 관측됐지만 보드 clock 정확도 증거는 아니다.

[normal](evidence/health-monitor-preboard-20260920/qemu-hello-production-normal/result.json), [probe](evidence/health-monitor-preboard-20260920/qemu-hello-probes-all/result.json), [capacity](evidence/health-monitor-preboard-20260920/qemu-hello-capacity4-capacity/result.json), [1초 리셋](evidence/health-monitor-preboard-20260920/qemu-hello-production-reset-1000/result.json), [protected 1초 만료](evidence/health-monitor-preboard-20260920/qemu-loadable_all-production-expiry-1000/result.json).

## 실제 CPU fault → 오류 메시지 전달

| Profile / 앱 | UDF 등록 | UDF 미등록 | MPU 등록 | MPU 미등록 |
|---|---|---|---|---|
| loadable_all / app1 | PASS | PASS | PASS | PASS |
| loadable_all / app2 | PASS | PASS | PASS | PASS |
| loadable_apps / app1 | PASS | PASS | PASS | PASS |
| loadable_apps / app2 | PASS | PASS | PASS | PASS |
| xip_all / app1 | PASS | PASS | PASS | PASS |

새 fault ELF 3개에서 메타데이터 추출과 fault 실행기를 모두 **Python `-O`**로 실행했다. 필수 breakpoint/ACK/검사가 최적화로 사라지지 않는 상태에서 다음을 관측했다.

1. 사용자 UDF 또는 MPU 위반, `CONTROL=3`, 원인 PID/binidx.
2. UDF의 CFSR `0x10000`, escalated HardFault IRQ 3/HFSR `0x40000000`; MPU의 MemManage IRQ 4, CFSR `0x82`, MMFAR `0x80000000`.
3. 기존 fault sender의 mq_send: 24B, priority 100, `BINMGR_FAULT=10`, 올바른 binidx.
4. Binary Manager mq_receive 직후 같은 24B 메시지 수신.
5. `binary_manager_recovery()` 호출 직전 중단, 전달될 binidx 확인.

20건 모두 `target_memory_or_register_writes=false`, `recovery_executed=false`, `unload_tested=false`다. GDB breakpoint와 읽기를 사용했으며 target의 fault PC/메시지 값을 써서 결과를 만들지 않았다. 메시지 이전의 `binary_manager_recover_userfault()`와 기존 thread 비활성화/sender 기동은 실행된다. 제외한 복구는 MQ 수신 이후 binary 복구/reload 호출이다. 긴 unload 중간 상태는 시험하지 않았다.

[UDF 등록 증거](evidence/health-monitor-preboard-20260920/qemu-loadable_all-fault-app1-udf-registered/result.json), [MPU 미등록 증거](evidence/health-monitor-preboard-20260920/qemu-xip_all-fault-app1-mpu-unregistered/result.json), [새 ELF 메타데이터](evidence/health-monitor-preboard-20260920/qemu-loadable_all-fault-build/fault-debug-metadata.json).

## 최초 기대값 오류 1건과 해결

초기 계획이 `CONFIG_BOARD_ASSERT_SYSTEM_HALT=y`인 loadable_all-production에 software reset을 기대하여 10초 대기가 실패했다. 같은 실행에서 HM 만료/로그/PANIC은 정상 관측됐다. ARMv8-M `_up_assert()`는 해당 설정에서 IRQ를 차단하고 정지하도록 컴파일된다.

제품 수정 없이 임시 orchestration의 기대값을 **HALT는 expiry, AUTORESET hello는 reset**으로 변경했다. 같은 새 ELF의 1/2초 expiry 재실행과 나머지 protected 구성은 통과했다. 최초 실패를 최종 유효 matrix 밖의 진단 기록으로 보존했으며 제품 결함 해결 건수로 세지 않는다.

[최초 실패](evidence/health-monitor-preboard-20260920/qemu-loadable_all-production-reset-1000/result.json), [원인·정책](evidence/health-monitor-preboard-20260920/diagnosis/assert-policy.json), [수정 전 matrix](evidence/health-monitor-preboard-20260920/diagnosis/matrix-before-policy-correction.json).

## 이전 증거와 남은 물리 보드 범위

이번 검증은 [리뷰 후속 보고서](Health_Monitor_Review_Fixes.md)의 최신 제품 변경에 대한 전체 빌드/실행 증거를 보완한다. 기존 host ASan/UBSan 22종, Python 프로토콜 18건, Cortex-A32 객체 15개와 독립 리뷰는 동일 제품 코드의 이전 증거로 연결하며 이번 실행 수에 합산하지 않는다. 과거 [QEMU 보고서](QEMU_ARMv8M_Health_Monitor_Validation.md)의 loader/수명주기 진단도 당시 기록으로 유지한다.

| 항목 | 현재 증거 | 남은 확인 |
|---|---|---|
| API / 수명 / wrap / 만료 로그 | host 모델 + 새 QEMU ARM UP 실행 | RTL 실제 제품 사용 흐름 |
| SMP 잠금 / 예약 사본 | host 동시성/경계, 대상 명령 검사 | 실제 2 CPU cache/IRQ 경합과 장시간 부하 |
| PM 준비·sleep·복귀 시간 보정 | 실제 소스 host 모델, Cortex-A32 컴파일, RTL 전체 링크 | 실제 CG/PG 반복, AON 정확도·유지, wakeup 지연 |
| watchdog / reset / reason | host, 설정·링크, QEMU software reset | WDG4 reset, 멈춘 tick/IRQ, 재부팅 reason 보존 |
| 성능 | 알고리즘 비용, 정적 메모리/TCB 실측 | KICK/STOP 시간, 다수 도래 예약의 ISR 최악 지연 |
| 사용자 CPU fault 메시지 | 새 protected ELF 20건 실제 예외→MQ | RTL 아키텍처 경로 |
| Binary Manager 복구 / 긴 unload | 사용자가 제외 | 현재 검증 대상 아님 |

물리 보드 항목은 소프트웨어 사전 검증으로 대신할 수 없다. 보드 수령 후 [시험 절차](../health-monitor/board-validation.md)를 이어서 실행한다. 이번 문서/증거 추가 때문에 동일 제품 코드의 전체 빌드를 반복하지는 않았다.
