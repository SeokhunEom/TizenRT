# Protected UMM `mm_sem` Signal Re-entry Deadlock Report

작성일: 2026-08-25
대상: `qemu-armv8m` `xip_all` (protected build, 단일 CPU)

## 문제와 범위

`mm_givesemaphore()`의 마지막 참조 해제는 기존에 아래 순서로 동작했다.

1. `heap->mm_holder = -1`
2. `heap->mm_counts_held = 0`
3. `sem_post(&heap->mm_semaphore)`

1과 3 사이에 같은 태스크의 signal handler가 `malloc()`을 호출하면
`mm_takesemaphore()`는 holder가 현재 PID와 다르다고 판단한다. semaphore는
아직 post되지 않았으므로 handler가 `sem_wait()`에서 block되고, 중단된 원래
경로는 `sem_post()`까지 도달하지 못한다. 결과는 self-deadlock이다.

이번 수정은 protected user-space UMM에만 적용한다. flat build와 kernel-side
heap은 기존 경로를 유지하며 public API를 바꾸지 않는다. `enter_critical_section()`은
protected UMM에서 사용할 수 없으므로 해결책으로 사용하지 않았다.

## 재현 방법

검증 중에만 `apps/examples/hello/hello_main.c`와 `os/mm/mm_heap/mm_sem.c`에
일회성 훅을 추가했다.

- `hello`가 `SIGUSR1` handler를 설치하고, handler에서 `malloc(32)`/`free()`를
  수행했다.
- 반복마다 outer `free()` 직전에 훅을 arm 했다.
- 마지막 release의 `mm_holder` 및 `mm_counts_held` 갱신 직후, `sem_post()` 직전에
  훅이 자기 자신에게 `SIGUSR1`을 보냈다.
- watchdog은 handler가 시작한 뒤 완료하지 못하고 진행률이 1초 동안 멈추면
  `MM_SEM_REPRO DEADLOCK`을 출력했다.
- 한 run은 20,000회 반복했다.

이 방식은 별도 sender thread의 heap 경합을 섞지 않고, 문제의 두 문장 사이에서
현재 태스크의 signal handler가 user heap에 재진입하도록 강제한다.

재현에 사용한 `hello` 변경, `mm_sem` 훅, QEMU 실행 스크립트와 raw 로그는 검증 후
모두 제거했다. 핵심 marker와 PASS/FAIL 값은 이 보고서에 기록했다.

## 수정 설계

`mm_holder`에 free 상태(`-1`)와 구별되는 내부 상태
`MM_SEM_RELEASING_HOLDER` (`-2`)를 추가했다. 이 상태는 protected user-space의
단일 CPU에서 마지막 참조를 해제하는 아주 짧은 구간에만 사용된다.

```text
outer free()
  sched_lock()
  mm_holder = MM_SEM_RELEASING_HOLDER
  mm_counts_held = 0
  [signal handler: take/try -> borrowed, give -> no-op]
  sem_post()
  mm_holder = -1
  sched_unlock()
```

- `mm_takesemaphore()`와 `mm_trysemaphore()`는 이 상태를 signal handler의
  borrowed release로 처리한다. semaphore를 기다리지 않고 held count도 늘리지
  않는다.
- 그 handler의 `mm_givesemaphore()`는 count를 소유하지 않았으므로 no-op으로
  돌아간다. 중단된 outer release가 유일하게 `sem_post()`를 수행한다.
- `sched_lock()`은 단일 CPU에서 normal task가 post와 free-state 전환 사이에
  실행되는 것을 막는다. signal action 자체는 scheduler lock 중에도 발생할 수
  있으므로, sentinel 처리 없이는 충분하지 않다.
- `CONFIG_SMP`에서는 이 상태 전이를 컴파일하지 않는다. SMP의 scheduler lock은
  다른 CPU에서 이미 실행 중인 태스크를 배제하지 않으므로, 검증하지 않은
  cross-CPU handoff를 이 변경으로 추가하지 않는다.

다음 대안은 채택하지 않았다.

- `enter_critical_section()`: protected UMM에서 호출할 수 없다.
- signal mask: 이미 pending인 signal과 signal dispatch 시점 문제 때문에 일반적인
  heap release 보호로 확정할 수 없었다.
- `sched_lock()`만 사용: signal action은 scheduler lock 중에도 실행될 수 있어
  원래의 same-PID 재진입을 막지 못했다.

## 검증 결과

| 검증 | 결과 | 근거 |
| --- | --- | --- |
| 수정 전 직접 signal 재현 | 실패 재현 | 20,000회 테스트에서 `MM_SEM_REPRO DEADLOCK` |
| 수정 후 직접 signal 재현 | 3/3 통과 | 각 20,000회 run이 PASS marker에 도달 |
| `qemu-armv8m xip_all` clean build | 통과 | protected user/common 및 kernel `mm_sem.c` 컴파일, XIP 패키지·파티션 크기 검증 통과 |
| QEMU `xip_all` kernel TC | 통과 | `Kernel TC End [PASS : 447, FAIL : 0]` |

직접 재현 runner는 PASS marker를 발견하면 QEMU를 종료하므로 일부 PASS 줄은
직렬 로그에서 잘려 있다. 그러나 PASS는 20,000회 loop 뒤에만 출력되고 runner의
세 run 모두 성공 종료했다.

## 제한 사항과 후속 검증

- `xip_all` 전체 runner는 두 번 실행했다. 두 run 모두 첫 IPv6 ping은 3/3 성공했지만,
  `ifdown`/`ifup` 뒤 두 번째 IPv6 ping이 `sendto error(113)`으로 실패했다.
  따라서 full network validation은 통과로 주장하지 않는다. 이 실패는 kernel TC
  실행 전 발생했고, IPv4/DHCP와 첫 IPv6 ping은 성공했다.
- 이 수정은 QEMU MPS2-AN505의 protected, non-SMP 구성에서 검증했다. protected
  SMP를 지원해야 한다면, 다른 CPU의 실행 태스크까지 포함하는 별도의 handoff
  동기화 설계와 SMP stress test가 필요하다.
- 실제 target hardware와 production signal source (`mq_send` 등)의 장시간
  stress test는 이번 QEMU 검증 범위에 포함하지 않았다.

## 영구 변경 파일

- `os/mm/mm_heap/mm_sem.c`
- `docs/Human/MM_Semaphore_Signal_Reentry_Report.md`
