# Health Monitor 리뷰 후속 수정·검증

2026-09-20. 수정 전 `cc00205af`의 리뷰에서 지적한 P2 두 건을 수정했다. 사용자가 선택한 최소 힙, deadline/check_at 분리, 단일 잠금, 잠금 없이 읽는 예약 사본과 STOP 선형 탐색을 유지했다. worker·범용 backend·추가 자료구조는 도입하지 않았다. 미래 PID 저장 방식에 대한 불필요한 주석을 줄이고, 선택 개선인 실제 만료 대상 로그를 추가했다.

## 수정 내용

- **Standards S1 — 실행기의 필수 동작과 판정:** Python `assert`에 있던 ACK 수신·breakpoint 설정/해제·펌웨어 및 fault/MQ 판정을 명시적 실행과 예외로 변경했다. 메타데이터 추출기도 같은 방식으로 정리했다. TCP checksum의 두 바이트가 나뉘어 도착해도 정확히 수신한다. Python `-O`/`-OO`에서도 검사와 중단 조건이 유지된다.
- **Spec F1 — PM 준비 시간:** 기존 `pm_sleep_ops`에 누적 `get_elapsedtick` 콜백 하나를 추가했다. RTL8730E는 systick 정지 때 미처리 ARM tick/소수 tick과 sleep 중에도 흐르는 32kHz 카운터를 기록한다. 최종 wakeup 조회에서 준비 시간을 빼고, 이미 도래했으면 sleep을 보류한다. 장치 복귀까지의 전체 구간을 한 번만 보정한다. 보류·실패 경로도 이전 sleep 표본을 사용하지 않는다. 재개 시 마지막 표본의 다음 tick 경계를 유지해 중복 보정과 반복 보류의 소수 tick 손실을 피한다.
- **보드 호환성:** 누적 측정 콜백이 없는 보드는 활성 Health Monitor의 sleep을 보류한다. 등록이 없으면 기존 보드 경로를 사용한다. HM/IRQ watchdog을 모두 끈 구성의 software watchdog도 최종 지연을 재계산한다. IRQ watchdog은 자체 하드웨어 경과 시간을 이미 계산하므로 준비 시간을 다시 빼지 않는다.
- **제품 만료 로그:** 만료 PID, 판정 now, 최신 deadline만 잠금 안에서 복사한다. 잠금 해제와 reboot reason 기록 뒤 기존 low-level 오류 로그로 출력하고 PANIC한다. DEBUG_ERROR/low-level 출력 설정을 따르며 만료 TCB 포인터는 잠금 밖에서 읽지 않는다. 현재 CPU thread의 기존 PANIC dump는 유지한다.

## 실행 결과

| 검증 | 결과와 의미 |
|---|---|
| Linux AArch64 ASan/UBSan 호스트 실행 파일 | **22/22 통과.** 기존 21종에 실제 RTL timer 소스 시험 추가. PM 준비125ms/timeout100ms는 추가 sleep 없이225로 보정, 준비25ms는 남은75ms sleep, resume 시간 포함, 실패·보류 및 기존 콜백 경로 검증. |
| RTL timer 소스 + 레지스터 모델 | 미처리 whole/fraction tick, 반복 sample, PG 후 ARM counter 초기화, AON 32비트 wrap, 보정 sample 이후 지연의 다음 ISR 처리, 반복 보류의 phase 보존 통과. |
| Python 프로토콜 회귀 | 일반·`-O`·`-OO` 각각6건, **18/18 통과.** breakpoint/ACK 실행 및 잘못된 ACK/checksum/짧은 메모리 응답/연결 종료를 실제 Remote 메서드로 확인. |
| QEMU 실제 CPU fault → 오류 메시지 전달 | **Python `-O` 20/20 통과.** 기존 저장 ELF 3 profiles, app1/app2 지원 조합, UDF/MPU, 등록/미등록. 예외→sender→MQ 수신→복구 호출 직전 중단. Binary Manager 복구·unload 실행은 제외. |
| GDB 메타데이터 추출 | `-O`에서 **3/3 통과**, 기존 저장 ELF의 메타데이터와 동일. |
| Cortex-A32 제품 객체 컴파일 | 대표 `rtl8730e/loadable_ext_ddr_st7785` 설정 기반 기본·오류 로그OFF·HM OFF·tick suppression OFF **15/15 통과**. PM/core/timer/idle 변경 파일 대상. 기존 `threads.h:thrd_join` 포인터 경고는 idle 객체에서 기록하고 오류 승격만 제외했다. 전체 펌웨어 링크/실행 증거는 아니다. |
| 독립 후속 리뷰 | Standards와 Spec 각각 원래 P2 해결 확인, 남은 구체적 결함 없음. HM OFF software wakeup 경계도 독립 재현 후 보완. |

[호스트 결과](evidence/health-monitor-review-fixes-20260920/host/result.json), [호스트 로그](evidence/health-monitor-review-fixes-20260920/host/test.log.gz), [QEMU matrix](evidence/health-monitor-review-fixes-20260920/qemu/matrix.json), [메타데이터 대조](evidence/health-monitor-review-fixes-20260920/metadata/result.json), [대상 컴파일](evidence/health-monitor-review-fixes-20260920/target/result.json), [Standards](evidence/health-monitor-review-fixes-20260920/standards-followup.md), [Spec](evidence/health-monitor-review-fixes-20260920/spec-followup.md).

QEMU 실행은 **실행기 수정 검증**이며 이전에 보존한 불변 펌웨어를 사용했다. 수정된 RTL PM 타이머나 제품 timeout 로그가 QEMU에서 실행됐다는 의미는 아니다. 로그의 잠금 해제 후 TCB 해제 경합은 ASan 호스트 시험으로, 실제 보드 헤더/명령 생성은 Cortex-A32 객체 컴파일로 확인했다. 물리 RTL8730E의 SMP 지연, 32kHz clock 정확도/절전 유지, watchdog reset, reboot reason 보존은 미검증이다. AON 차이는 기존 32비트 카운터의 한 바퀴 이내 구간을 전제로 한다.

현재 PM 진입은 전역 critical section과 secondary idle 확인 아래 수행된다. 현재 보드 PM 콜백에서 Health Monitor START/KICK을 호출하는 경로는 없다. START API가 idle PID를 별도로 거부하는 것은 아니므로 그런 정책이 있다고 가정하지 않았다.

## 재실행과 이력

```sh
make -C os/kernel/health_monitor/tests test OUT_DIR=/tmp/hm-tests
python3 tools/qemu-armv8m-health-monitor/test-fault-message.py
python3 -O tools/qemu-armv8m-health-monitor/test-fault-message.py
python3 -OO tools/qemu-armv8m-health-monitor/test-fault-message.py
```

호스트 C 시험은 Linux 환경을 사용한다. 보드 객체 컴파일·QEMU matrix의 정확한 명령과 입력 경로는 해당 JSON과 재현 스크립트에 보존했다. 코드 해시는 [source-manifest.json](evidence/health-monitor-review-fixes-20260920/source-manifest.json)을 기준으로 대조한다.

수정은 새 독립 기능 커밋을 남기지 않고 registry/timeout/PM/watchdog/ARMv8M 검증의 관련 기존 커밋에 통합했다. 수정 전 이력은 로컬 `backup/health-monitor-before-review-fixes-20260920`으로 보존한다. 원격 push는 이번 요청 범위에 없어 수행하지 않았다. GitHub ref 조회에서 원격은 기존 `cc00205af` 그대로임을 확인했다. [최초 리뷰](Health_Monitor_Main_Code_Review.md)와 그 증거의 기존 SHA는 당시 상태를 나타내며 새 결과로 덮어쓰지 않는다.

최종 rebase 뒤 검증 전·후 트리가 완전히 동일함을 확인하고 22종 호스트 시험을 재실행했다. timer 커밋 `cfc4cc700`과 PM 커밋 `22f254cfa`도 각각 임시 checkout에서 그 시점의 전체 호스트 시험을 통과했다. [통합 후 검증](evidence/health-monitor-review-fixes-20260920/post-rebase/result.json). 이후 변경은 이 결과를 기록하는 문서·로그 추가뿐이며 제품/테스트 코드 해시는 동일하다.
