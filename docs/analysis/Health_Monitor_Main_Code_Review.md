# Health Monitor — main 대비 설계·코드 리뷰

후속 상태: 아래는 수정 전 고정 SHA의 리뷰 기록이다. 사용자가 승인한 P2 두 건 수정과 선택적 만료 로그 개선을 완료했다. [수정·검증 결과](Health_Monitor_Review_Fixes.md)를 현재 상태로 사용한다.

2026-09-20 최초 리뷰 결론은 **전면 재설계가 필요한 구조는 아니며, P2 두 건의 보완을 권고**한다. Standards/코드 품질 축에서는 시험 실행기의 동작 결함 한 건, Spec 축에서는 PM 시간 계산의 통합 문제 한 건을 재현했다. 코어의 최소 힙·KICK 지연 갱신·전용 잠금·예약 정보 게시 자체는 현재 계약에 비례한 설계다. 제품 코드의 메모리 손상이나 새로운 교착을 입증한 P0/P1은 발견하지 못했다. 이것이 실제 SMP/절전/하드웨어 watchdog의 안전성을 증명한다는 뜻은 아니다.

최초 리뷰 시점에는 제품 코드를 수정하지 않았다. 이후 승인된 수정 및 기존 커밋 통합은 후속 보고서에 구분해 기록한다.

## 기준과 범위

| 항목 | 고정 값 |
|---|---|
| main 및 조회한 origin/main | `080481ee8bea3511ec5b0b8baac6d0a713f571aa` |
| Health Monitor HEAD | `cc00205afc56e938132403dc3d3aae65db8e8cb6` |
| merge-base | `644d01e56950e331accc14e99855304af7dc5ba9` |
| 기능 도입 전 부모 | `79afdbf91a9f0005fdf4e92a83f420fe46487b3a` |
| 주 비교 | `git diff 080481ee8...cc00205af` |
| 기능 변경의 출처 구분 | `git diff 79afdbf91 cc00205af` |

main의 분기 이력이 달라 주 비교에는 903개 파일과 main에 없는 105개 커밋이 있다. 이를 전부 Health Monitor가 만든 변경으로 취급하지 않았다. 기존 Task Monitor 제거부터 현재까지의 기능 관련 10개 커밋을 추적했다. 기능 변경에는 증거 자료를 제외한 162개 경로가 있고, 그중 54개는 설정, 34개는 테스트를 제외한 OS 경로다. media/FOTA 등 다른 기능의 유입 변경은 Health Monitor 결함 집계에서 제외했다. 삭제 경로와 빌드 연결도 리뷰 범위에 포함했다.

[고정 SHA·커밋·경로 목록](evidence/health-monitor-main-review-20260920/manifest.json), [main 이후 전체 커밋](evidence/health-monitor-main-review-20260920/main-commits.txt).

명세는 [구현 계획](../HealthMonitorImplementationPlan.md), [단계별 구현 문서](../health-monitor/README.md), 공개 API 주석을 사용했다. 저장소의 [CodingStyleGuide](../CodingStyleGuide.md)와 기존 C 커널 관례도 대조했다. 저장소에 `docs/agents/issue-tracker.md`가 없어 외부 이슈와의 추가 대조는 하지 않았다. Standards와 Spec을 독립 검토한 뒤 재현과 주변 호출 경로를 확인했다. Binary Manager 복구 및 unload 지연의 실행 검증은 사용자 요청대로 제외했다.

## 동작을 보는 관점

앱은 `/dev/health_monitor`에 START/KICK/STOP을 전달한다. fd에는 감시 대상 소유권을 저장하지 않고 매 호출의 현재 thread를 대상으로 한다. TCB에는 최신 deadline/timeout만 두고, 정적 최소 힙은 다음에 확인할 `check_at`을 보관한다.

KICK은 최신 deadline만 바꾼다. 따라서 KICK마다 힙을 정렬할 필요가 없다. CPU0 tick은 먼저 TCB 포인터가 없는 예약 사본을 읽는다. 도래한 예약이 있을 때만 잠금을 한 번 시도하고, 최신 deadline을 다시 읽어 만료하거나 예약을 갱신한다. PM도 같은 예약 사본을 읽지만 힙을 수정하지 않는다. ISR 검사가 정상적으로 완료된 경우에만 HW watchdog을 갱신한다.

이 구분은 불필요한 계층이 아니다. 최신 deadline과 검사 예약을 합치거나, 잠금 없이 힙의 TCB를 읽거나, 매 tick 모든 TCB를 순회하면 각각 KICK 비용·수명 보호·평상시 tick 비용 계약을 잃는다.

## Standards

### S1 — P2: Python assert 안의 필수 디버거 동작

위치: `tools/qemu-armv8m-health-monitor/fault-message.py:67,86`; ELF/config 확인은 같은 파일 `102–106` 등.

```python
assert self.socket.recv(1) == b'+'
assert self.request(('Z' if enabled else 'z') + f'1,{address:x},2') == 'OK'
```

`assert`에는 검증뿐 아니라 ACK 수신과 breakpoint 등록/해제라는 필수 동작이 들어 있다. Python `-O` 또는 `PYTHONOPTIMIZE`에서 표현식 전체가 제거되므로 프로토콜 동작도 사라진다. 같은 모드에서는 ELF 일치, fault 종류, 메시지 내용 등의 판정도 제거된다. 단순 포맷 취향이나 추상화 선택 문제가 아니라 시험 실행기의 동작 결함이다. 저장소의 C 스타일 강제 규칙 위반으로 분류하지는 않는다.

현재 파일의 `Remote` 클래스를 AST로 추출해 실제 `breakpoint(0x1234)` 메서드를 실행했다. 정상 모드에서는 `Z1,1234,2` 요청 한 건이 발생했고 최적화 모드에서는 요청이 없었다. 이 재현은 QEMU를 실행하지 않았다. 전체 최적화 실행은 ACK 처리 문제로 일찍 실패할 수도 있으므로 **실제 복구가 실행됐다거나 거짓 PASS가 발생했다고 주장하지 않는다.** 기존 일반 모드 20/20 결과도 이 발견만으로 무효화하지 않는다.

수정 방향은 요청/수신을 무조건 실행한 뒤 반환값을 명시적으로 검사하고 실패 시 예외를 발생시키는 것이다. firmware와 결과 판정도 최적화로 제거되지 않아야 한다. 가장 작은 임시 방어는 QEMU 실행 전에 `sys.flags.optimize`를 검사해 해당 모드를 거부하는 것이다. 새 추상화나 디버거 프레임워크는 필요하지 않다.

[독립 검토](evidence/health-monitor-main-review-20260920/standards/review.md), [원래 재현 결과](evidence/health-monitor-main-review-20260920/standards/assert-side-effect-repro.json).

### 강제 규칙과 클린코드 판단

검토한 제품 코드에서 의미 있는 CodingStyleGuide 강제 위반은 발견하지 못했다. 공개 ioctl 헤더와 커널 내부 헤더의 책임 분리는 M09 취지에 맞는다. 의미를 숨기는 범용 프레임워크, 불필요한 factory/strategy, 제품 코드의 중복 상태 머신도 발견하지 못했다.

정적 함수별 책임은 비교적 분명하다. START의 검증/삽입, KICK의 deadline 갱신, STOP/cleanup의 공통 해제, timer의 검사/갱신으로 나뉜다. 오류 반환과 자원 소유권도 공개 주석과 구현이 맞는다. 문서화된 동기화 불변조건을 단순히 주석이 길다는 이유로 삭제하는 것은 권하지 않는다. 반복적인 절차 설명의 축약은 가능하지만 우선순위가 낮다.

## Spec

### F1 — P2: PM 준비 시간 누락을 새 감시 wakeup 계산이 그대로 전제함

변경 위치: `os/pm/pm_idle.c:312,507`. 관련 기존 경로: 같은 파일 `579`, `os/board/rtl8730e/src/component/soc/amebad2/misc/ameba_tizenrt_pmu.c:210,224–232`, `os/arch/arm/src/amebasmart/amebasmart_timerisr.c:118`.

명세 [구현 계획 181행](../HealthMonitorImplementationPlan.md)은 다음을 요구한다.

> 이미 검사할 시각이 됐다면 sleep 진입을 보류하고 timer 경로에서 검사하게 한다.

추가된 최종 재조회는 suspend 콜백과 다른 CPU 정지 뒤 실행되지만 `clock_systimer()`를 다시 읽는다. 이 구간에서는 CPU0 IRQ가 이미 막혀 있어 시스템 tick이 진행하지 않는다. 기준 보드의 누락 시간 측정은 더 늦은 실제 sleep 함수 내부에서 시작하고, 복귀 시 timer comparator도 새로 설정한다. 따라서 준비 시간은 최종 감시 wakeup 지연에서 빠지고, 이후 tick 보정에도 포함되지 않는다.

수정하지 않은 실제 PM/HM 구현과 기존 호스트 fixture로 다음 조건을 재현했다.

| 단계 | OS tick | 독립적인 경과 시간 기준 시각 |
|---|---:|---:|
| START(100ms), deadline=200 | 100 | 100 |
| IRQ가 막힌 suspend 준비에 125ms 소요 | 100 | 225 |
| 최종 재조회가 추가 100ms sleep을 허용 | 100 | 225 |
| sleep 100ms만 보정 후 복귀 | 200 | 325 |

즉 준비 중 이미 지났어야 할 감시 예약에 대해 추가 sleep을 허용한다. 실제 RTL watchdog 포트와 vendor register 모델을 포함한 5초 WDT 구성에서도 같은 결과를 재현했다. 더 긴 하드웨어 watchdog이 짧은 감시 deadline의 wakeup 계산을 대신하지는 않는다.

**원인 출처를 구분해야 한다.** 준비 시간 누락 자체는 기존 보드 PM 시계의 한계이며 이번 기능이 처음 만든 일반 타이머 버그가 아니다. 새 Health Monitor PM 연결이 그 시간 계산을 충분하다고 가정한 통합 문제다. 명세에 없는 고정 100ms 탐지 상한이나 즉시 PANIC을 요구하는 지적도 아니다. 확인할 시각이 지난 후 불필요한 sleep을 더 허용하는 조건에 관한 지적이다.

수정 방향은 IRQ가 막힌 PM 전환 전체의 경과 시간을 같은 단조 시계로 추적하고, 최종 wakeup 선택과 tick 보정에 정확히 한 번 반영하는 것이다. 단순히 `clock_systimer()` 조회 횟수를 늘리는 것으로는 해결되지 않는다. 보드에서 전체 경과 시간을 안전하게 계산할 수 없다면 감시가 활성화된 해당 sleep 경로를 보류하는 정책도 검토할 수 있다. 제품 코드 변경 전에는 suspend 지연, sleep 진입 실패, 정상 복귀에서 누락·중복 보정이 없는지 함께 검증해야 한다.

[독립 검토](evidence/health-monitor-main-review-20260920/spec/spec-review.md), [재현 출력](evidence/health-monitor-main-review-20260920/spec/pm-repro.log). 이는 ASan/UBSan 호스트 재현과 실제 보드 소스 호출 순서를 대조한 근거다. 물리 보드 재현은 아니다.

## 설계·오버엔지니어링 판단

| 설계 선택 | 판단과 이유 |
|---|---|
| TCB 8바이트 + 정적 최소 힙 | 현재 메모리 제약과 수명 연결에 맞는다. 동적 registry나 별도 worker를 추가할 이유가 없다. |
| deadline/check_at 분리 | O(1) KICK을 위한 핵심 설계다. 조기 검사를 허용하는 정책과 일치한다. |
| STOP의 O(N) 탐색 | 최대 256개와 명시된 비용 계약에서 수용 가능한 선택이다. 실측 없이 TCB index를 추가할 필요는 없다. |
| spinlock 하나 | 상태·힙·TCB 수명을 같은 범위에서 보호한다. 여러 잠금으로 분할할 필요를 입증하지 못했다. |
| ISR의 단회 weak CAS | 일반 ARM trylock의 재시도 가능성을 피하려는 실제 요구다. 일반화가 아니라 ISR 제약 대응이다. |
| pointer-free 예약 사본 + sequence | PM/정상 tick이 잠금을 기다리거나 종료 중 TCB를 읽지 않도록 하는 필요 장치다. 단순 volatile로 대체하면 안 된다. |
| health_monitor_state() 접근 함수 | 현재 계획에서 요구한 작은 저장 위치 경계다. 함수 포인터나 선택형 backend가 없어 과도하지 않다. |
| 별도 ioctl 드라이버 | fd 공유 시 현재 호출자 소유권과 by-value 인자를 분명하게 한다. 별도 세션/객체 계층이 필요하지 않다. |
| 코드 밖의 시간/레지스터 모델 | 보드 없이 실패 조건을 재현하는 데 필요하다. 다만 모델 성공을 실제 ARM SMP/PM 성공으로 확대하면 안 된다. |

여기서 O(1)은 자료구조 작업량이다. thread 쪽 spinlock 대기까지 wait-free이거나 지연 상한이 보장된다는 뜻은 아니다. 도래 후보를 모두 처리하는 O(N log N) 구간과 다른 CPU의 잠금 대기 시간은 보드에서 측정해야 한다. 현재 계약은 고정 후보 수 제한을 금지하므로 임의의 quota를 추가하는 것도 권하지 않는다.

선택적으로 다시 결정할 설계는 **제품 장애의 대상 식별**이다. `health_monitor.c:578–604`에서 만료 PID/deadline 보존은 QEMU 시험 설정에만 있다. 제품에서는 reason 62와 기존 PANIC이 남고, 그 진단 대상은 실제 만료 thread가 아닌 CPU0의 현재 thread일 수 있다. 원인 분석에 불편하지만 [계획 196행](../HealthMonitorImplementationPlan.md)이 의도적으로 허용한 범위라 Spec 결함이나 필수 수정으로 세지 않았다. 현장 분석에 필요하다면 잠금 안에서 PID/deadline 같은 작은 값만 복사하고 잠금 밖에서 출력하는 정도가 적절하다. TCB 포인터를 잠금 밖에서 다시 읽거나 새로운 dump framework를 만드는 것은 피해야 한다.

## 경로별 확인 결과

| 확인 영역 | 결과 또는 남은 경계 |
|---|---|
| START 인자·중복·용량·errno | 공개 계약과 구현 일치. 실패 시 상태 변경 없음. 넓은 unsigned long의 잘림 방어 존재. |
| KICK/STOP·fd 공유·close | caller thread 기준. close가 STOP이 아닌 의미도 문서와 일치. |
| tick 변환·0·wrap·half-range | 올림 변환과 INT32_MAX 제한, 공통 now 기준 힙 비교가 일관됨. 무한 기간 지연까지 지원하지 않는 경계 명시. |
| 갱신된 루트 뒤의 만료 대상 | 고정 now로 도래 후보를 모두 처리하므로 반복 갱신된 루트가 만료 대상을 숨기지 않음. |
| TCB 초기화·재시작·해제 | init/공통 cleanup/release-before-PID 재사용 연결 확인. 새로 입증한 UAF 없음. 실제 SMP teardown 실측은 별도. |
| lock 순서·외부 호출 | private lock 아래에서 scheduler lock·malloc·로그·PANIC을 추가 호출하는 역순 경로를 발견하지 못함. |
| 잠금 없는 hint | atomic word 접근과 sequence/fence 규칙, 실패 시 -EAGAIN 처리 확인. ARM 캐시 경합 실측을 대체하지 않음. |
| 만료 확정·PANIC | 잠금 해제 후 reason 기록/PANIC. 이후 만료 TCB를 역참조하지 않음. 대상 식별은 위 선택 사항. |
| PM 선택·필수 콜백·긴 지연 | 예약 0과 무예약 구분, 콜백 부족 시 보류, µs 변환 포화는 적절. 준비 시간 통합은 F1. |
| HW watchdog | 단일 소유자, 실제 start, 완료된 검사 뒤 keepalive, CPU0 제한, 절반 주기 wakeup budget 확인. 물리 reset/절전 clock은 미검증. |
| Kconfig·기능 OFF | tickless/억제 timer 제외, 조건부 TCB/연결, 대표 설정 활성화와 시험 설정 분리 확인. 모든 보드를 새로 빌드한 것은 아님. |
| legacy Task Monitor 제거 | 별도 관련 커밋이며 prctl 번호를 예약 자리로 유지함. 기존 API 사용 앱에는 마이그레이션이 필요함. 자동으로 모든 기존 thread를 새 감시에 등록하지 않음. |
| 테스트 실행기·증거 | 일반 모드 이전 증거는 보존. 필수 프로토콜 동작/판정의 assert 의존은 S1. |

실제 제품에서 감시할 스레드는 START/KICK을 사용해야 한다. 옵션을 켜고 드라이버를 등록하는 것만으로 모든 thread가 감시되지는 않는다. 이는 자기 등록 방식의 계약이며, 기존 앱의 감시 적용 여부는 배포 판단에서 별도 확인해야 한다.

## 이번에 실행한 검증과 증거 한계

- 고정한 Health Monitor HEAD를 Docker에 **읽기 전용**으로 마운트하고 Linux AArch64에서 기존 ASan/UBSan 호스트 실행 파일 21종을 모두 실행했다. 8.196초, 모두 통과. [명령·결과](evidence/health-monitor-main-review-20260920/host-tests/result.json), [로그](evidence/health-monitor-main-review-20260920/host-tests/test.log.gz).
- PM 준비 지연 재현 두 종류와 실제 Remote 메서드의 optimize=0/1 대조를 실행했다. [패키지 재실행 결과](evidence/health-monitor-main-review-20260920/reproduction-result.json).
- 코어·주변 호출 경로와 기준 보드 시간 보정을 대조했다. 현재 변경을 입증하지 않는 단순 코드 취향이나 과거 메모리의 다른 설계를 강제하지 않았다.
- 이번 리뷰에서 QEMU CPU fault/Binary Manager 복구를 다시 실행하지 않았다. [기존 ARMv8M 검증](QEMU_ARMv8M_Health_Monitor_Validation.md)의 20개 메시지 전달 결과는 기존 실행 증거이며, 복구·unload 성공을 뜻하지 않는다.
- 물리 RTL8730E의 SMP 캐시·IRQ 최악 지연·PM clock gating·watchdog reset·reason 보존을 새로 검증하지 않았다. 특히 F1은 보드 실측 수치가 아닌 host 모델 재현이다.

아래 재현 명령은 수정 전 HEAD를 요구하는 역사적 재현이다. 로컬 백업 `backup/health-monitor-before-review-fixes-20260920`의 별도 checkout에서 실행하며, 수정 후 회귀는 후속 보고서의 테스트를 사용한다. 재현은 제품 파일을 고치지 않고 임시 폴더에 빌드한다. `findings_reproduced`는 발견한 문제 조건이 관찰됐다는 뜻이며 제품 요구사항 PASS가 아니다.

```sh
python3 docs/analysis/evidence/health-monitor-main-review-20260920/reproduce.py
```

Standards: P2 1건(S1), 의미 있는 강제 C 스타일 위반 0건. Spec: P2 1건(F1). 두 축의 지적은 별개이며, 대상 식별 개선은 선택 사항으로만 남긴다.
