# Health Monitor QEMU 검증 결과

2026-09-20 KST. 클록 수정과 아래 QEMU 단일 코어 검증표를 완료했다. 이전 보고서에 남아 있던 API 계약, 강제 종료·재시작, PID/TCB 재사용, 다중 감시, 시간 경계 항목을 QEMU에서 추가 검증했다. 원격 CI와 실제 보드 검증은 포함하지 않는다.

## 브랜치와 검증 소스

- 클록 수정: **`ceef1353723842ed051379d6f370319d4f00dfaf`**, `codex/qemu-build-test`에 push했다. 변경 파일은 `os/arch/arm/src/tiva/tiva_syscontrol.c` 하나이며, 이 보고서를 보관한 Health Monitor 브랜치에는 클록 커밋을 합치지 않았다.
- 검증 대상은 위 QEMU 커밋에 Health Monitor 원본 **`8d10c93bd79c69fb2b64ab420111a415e3d17eeb`**와 시험 코드를 임시 통합한 소스다. 원래 QEMU의 pthread/mutex 복구 처리를 유지했으며 다른 보드 defconfig나 legacy task_monitor 제거는 가져오지 않았다.
- 검증 앱, 시험 hook, 실행기, 이 보고서와 결과 증거는 **`codex/260901-health-monitor`**에 보관한다. 기존 Health Monitor 구현에 추가된 코드만 반영하고, QEMU 전용 defconfig·장치 등록 연결을 포함한 전체 검증 소스는 [재현 패치](evidence/health-monitor-qemu-20260920/qemu-overlay.patch)로 보존한다.
- 따라서 아래 QEMU 결과는 **위 두 소스를 조합한 펌웨어**의 결과다. Health Monitor 브랜치 전체를 그대로 빌드하여 QEMU에서 실행한 결과로 해석하지 않는다. 대상 브랜치에서 다시 수행한 호스트 테스트와 소스 일치 검사는 [이전 검증](evidence/health-monitor-qemu-20260920/transfer-verification.json)에 별도로 기록한다.
- 최종 QEMU 시험 펌웨어는 Health Monitor와 기존 `health_monitor` 예제를 켜고 **`CONFIG_EXAMPLES_HEALTH_MONITOR_QEMU`와 작은 용량 설정은 껐다.** ELF에서 `hm_qemu`/`g_hm_qemu` 심볼이 없고 Health Monitor 심볼은 존재함을 확인했다.

## 클록 수정과 검증

LM3S 클록 초기화는 oscillator가 이미 켜져 있으면 PLL/divider 설정까지 건너뛰고 있었다. oscillator 활성화·안정화 대기만 조건부로 남기고 PLL/divider 설정은 실행하도록 고쳤다. LM4F/TM4C/CC3200의 별도 분기는 변경하지 않았다.

| 항목 | 수정 전 | 수정 후 |
|---|---:|---:|
| QEMU 시스템 클록 | 12.5MHz | 50MHz |
| OS 설정 tick | 10ms | 10ms |
| 레지스터로 계산한 실제 SysTick | 40ms | 10ms |
| RCC | `0x078e3ac0` | `0x01ce1380` |
| RCC2 | `0x07802810` | `0x01800000` |
| SysTick RELOAD | `0x0007a11f` | 동일 |

클록 전용 검사는 수정 전 `SysTick period does not match CONFIG_USEC_PER_TICK`으로 실패하고 수정 후 통과했다. 최종 펌웨어에서도 다시 50MHz/10ms를 확인했다. 실제 timeout 1000ms/2000ms는 각각 **0.996초 / 1.996초**에 관측됐다. 판정 허용 오차는 max(150ms, 요청 시간의 5%)이며, 호스트 UART 수집 시각이므로 실제 보드의 정밀 시간 측정으로 환산하지 않는다.

[수정 전 검사](evidence/health-monitor-qemu-20260920/clock-before/result.json), [최종 클록 검사](evidence/health-monitor-qemu-20260920/final-clock/result.json), [최종 1초 만료](evidence/health-monitor-qemu-20260920/final-expire-1000/result.json), [최종 2초 만료](evidence/health-monitor-qemu-20260920/final-expire-2000/result.json).

## 추가 QEMU 검증 범위

검증 앱은 [health_monitor_qemu](../../apps/examples/health_monitor_qemu/health_monitor_qemu_main.c), 실행기는 [health-monitor-extended.py](../../tools/qemu-build-test/health-monitor-extended.py)다. 실제 ARM 펌웨어의 VFS, variadic ioctl wrapper, 레지스트리, 스케줄러, task/pthread 수명 경로를 실행했다.

| 영역 | 확인한 내용 | 결과 |
|---|---|---|
| START | 0 거부, 중복 거부 및 기존 상태 유지, 1/9/10/11ms tick 올림, UINT32_MAX 인자 | 통과 |
| KICK/STOP | 등록·미등록 호출, 늦은 KICK, 중복 STOP, 호출 스레드만 갱신 | 통과 |
| VFS | 잘못된/닫힌 fd의 EBADF, 알 수 없는 명령의 ENOTTY, 길이 0/1 read·write의 ENOSYS | 통과 |
| open/close | open이 감시를 시작하지 않음, close가 감시를 끝내지 않음, reopen 후 등록 유지 | 통과 |
| 공유 fd | 두 스레드의 독립 deadline, 한쪽 KICK/STOP/exit가 다른 쪽 감시를 해제하지 않음 | 통과 |
| 수명주기 | pthread_cancel, pthread_exit, task_delete, 정상 task return, 같은 PID/TCB의 task_restart | 통과 |
| 재사용 | 감시하던 스레드 종료 후 같은 PID 93회, 같은 TCB 주소 93회 재사용 및 새 상태 확인 | 통과 |
| 동시 감시 | worker 7개, 서로 다른 timeout, 주기적 KICK과 파일 read/write 부하 | 통과 |
| 반복 등록 | START→KICK→STOP **210,000사이클** 및 마지막 등록 상태에서 exit cleanup | 통과 |
| 시간/힙 경계 | 검사 전 늦은 KICK, lazy root 갱신, deadline 이후 STOP, wraparound 생존, 미완료 게시 시 검사 보류 | 통과 |
| 공간 부족 | 실제 스레드 4개로 heap 포화 → ENOSPC → STOP 후 새 등록 성공, 3회 반복 | 통과 |

확장 정상 시험은 한 번 부팅한 상태에서 위 7개 정상 명령을 3회 반복했고 회차별 **2,045개 검사**, 총 **6,135개 검사**를 수행했다. 종료 후 task 집합은 원래 8개로 복귀했으며 heap used는 **76112 / 76112 / 76112 bytes**로 동일했다. 반복 구간에서 증가가 없다는 증거이며 모든 가능한 누수의 부재를 뜻하지 않는다.

기본 MAX_TASKS=16 구성에서는 시스템 task와 관찰 task가 슬롯을 점유하므로 감시 heap 전체 16개를 사용자 worker로 채울 수 없다. ENOSPC 분기는 **정적 감시 용량만 4개로 낮춘 별도 시험 펌웨어**에서 실제 스레드로 검증했다. 가짜 TCB를 registry에 끼워 넣지 않았다.

PID 재사용은 할당 cursor를 이전 PID 직전으로 되돌려 실제 PID allocator가 동일한 빈 슬롯을 선택하도록 했다. TCB 주소는 allocator가 실제로 재사용한 것을 관찰했으며 강제로 주소를 바꾸지 않았다. 새 스레드를 미등록 상태로 유지하면서 이전 deadline 이후 생존하는지도 확인했다.

## 만료 및 PANIC 시나리오

각 시나리오는 독립된 QEMU 부팅에서 수행했다. 공통 assertion 안내만으로 성공 처리하지 않고, Health Monitor의 정확한 PANIC 파일/줄, 실제 만료 PID·검사 시각·deadline, SysTick IRQ 15, heap corruption 없음까지 확인했다.

| 시나리오 | 만료 PID | 검사 tick | deadline | 결과 |
|---|---:|---:|---:|---|
| close만 한 스레드의 만료 | 9 | 24 | 24 | 통과 |
| now == deadline | 9 | 102 | 102 | 통과 |
| now > deadline | 9 | 103 | 102 | 통과 |
| 늦은 KICK 후 갱신된 deadline 만료 | 9 | 105 | 105 | 통과 |
| wraparound 후 deadline == 0 | 9 | 0 | 0 | 통과 |
| 갱신된 오래된 root 뒤의 만료 대상 | 11 | 103 | 103 | 통과 |
| 일부만 KICK하는 실제 동시 감시 | 11 | 24 | 24 | 통과 |
| 동일 deadline 다중 대상 | 10 | 102 | 102 | 통과 |
| 최대 timeout 대상과 단기 만료 공존 | 11 | 103 | 103 | 통과 |
| 게시 중단 해제 후 만료 처리 | 9 | 103 | 102 | 통과 |

`close`와 `multi`는 자연스럽게 진행하는 실제 시스템 시간을 사용했다. 나머지 경계 시나리오는 시험용 설정에서 **Health Monitor가 읽는 시각만 제어**하고 실제 SysTick이 판정했다. OS 전체 시계·스케줄러·VFS를 호스트 모형으로 교체하지 않았다. `unstable`은 게시 sequence를 일시적으로 홀수 상태로 만들어 보류/복구를 검증한 주입 시험이며 실제 SMP 경쟁 증거가 아니다.

PANIC의 `task: Idle Task` 등은 IRQ가 발생한 순간의 실행 문맥이다. 만료된 감시 대상은 잠금 안에서 PID/deadline 값을 복사한 시험 진단으로 별도 확인했다.

시험 hook은 [tests/qemu](../../os/kernel/health_monitor/tests/qemu/probe.h)에 격리되어 있고 최종 펌웨어에서는 컴파일되지 않는다. 만료 대상 진단 로그는 감시 잠금을 해제한 뒤 출력한다. QEMU PANIC 정책은 system halt이며 수집 후 실행기가 프로세스를 종료했다. **실제 HW 리셋이나 reboot reason 유지까지 검증한 것은 아니다.**

## 시험용 제어를 끈 최종 회귀 검증

최종 펌웨어를 clean build한 뒤 원래 `health_monitor` 예제의 정상 KICK·STOP·100회 worker 종료를 3회 반복하고, 커널 테스트 **433 PASS / 0 FAIL** 및 이후 정상 KICK 재실행을 통과했다. 별도 부팅의 1000ms/2000ms 만료도 정확한 source PANIC과 경과 시간을 확인했다.

기존 전체 QEMU workload는 **한 번 부팅하여 3회차** 실행했다. 각 수치는 PASS/FAIL이다.

| 회차 | Network | libc++ | Filesystem | Kernel | Drivers | Heap used |
|---|---:|---:|---:|---:|---:|---:|
| 1 | 163/0 | 795/0 | 203/0 | 433/0 | 14/8 | 77968 |
| 2 | 163/0 | 795/0 | 203/0 | 433/0 | 14/8 | 77968 |
| 3 | 163/0 | 795/0 | 203/0 | 433/0 | 14/8 | 77952 |

매 회차 network peer, C++ smoke, SmartFS 100회 fill/delete도 수행했다. 최종 상태는 `pass_with_known_driver_failures`다. Drivers의 8개 FAIL은 기존 PWM/watchdog/ADC 미지원 signature와 장치 부재를 대조해 허용한 결과이며 새로운 실패는 없다. 2→3회차 heap 증가 **-16 bytes**, task 집합·파일·파일시스템 용량과 저장 sentinel도 유지됐다.

같은 소스의 기존 Linux AArch64 호스트 테스트 **21종**과 watchdog 설정 거부 검사를 ASan/UBSan을 켠 상태로 다시 통과했다. 이 호스트 SMP/PM/watchdog fixture는 QEMU나 실제 보드 실행 증거와 구분한다.

## 환경·산출물·재현

- LM3S6965EVB / Cortex-M3 / UP / flat / RAM 16MiB / MAX_TASKS=16 / OS tick 10ms.
- Docker image: `sha256:8f2d15b7d82cf8c58a9092ec0dcc1ed1bbda9721a6cf19cc832c4eb9a48f8496`.
- ARM GCC 10.3.1, QEMU 2.12.0, Python 3.5.2. Runtime 컨테이너의 소스는 read-only이며 네트워크는 껐다. full-set의 guest 네트워크 시험은 컨테이너 내부 QEMU user networking/peer를 사용한다.
- 최종 ELF SHA-256: `2acf494af7f0e72c0bb92e693df0fa3e3f743f97cf04e7ea445daf60d05dc222`. 정상/만료/full-set 결과가 모두 이 ELF hash와 일치한다.
- 클린 빌드 로그의 C++ 전용이 아닌 옵션 관련 기존 진단 2줄은 이전 빌드와 동일하다. make 종료 0, ELF 링크, 실제 runtime 결과를 각각 확인했으며 진단 없는 빌드라고 주장하지 않는다.

주요 증거:

- [기계 판독 요약](evidence/health-monitor-qemu-20260920/summary.json), [세부 검증표 37행](evidence/health-monitor-qemu-20260920/coverage.json)
- [클록 검사 스크립트](evidence/health-monitor-qemu-20260920/clock-check.py), [빌드 도구](evidence/health-monitor-qemu-20260920/build.py), [최종 빌드](evidence/health-monitor-qemu-20260920/final-build/result.json)
- [확장 검증 전체 결과](evidence/health-monitor-qemu-20260920/matrix-status.json), [확장 정상 3회차](evidence/health-monitor-qemu-20260920/matrix-all/result.json), [정상 직렬 로그](evidence/health-monitor-qemu-20260920/matrix-all/serial.log)
- [용량 4개 시험](evidence/health-monitor-qemu-20260920/capacity-test/result.json), [용량 시험 펌웨어](evidence/health-monitor-qemu-20260920/capacity-build/result.json)
- [시험 hook 제외 확인](evidence/health-monitor-qemu-20260920/production-mode.json), [최종 정상·커널 결과](evidence/health-monitor-qemu-20260920/final-normal/result.json)
- [전체 3회차 결과](evidence/health-monitor-qemu-20260920/full-set/result.json), [전체 직렬 로그](evidence/health-monitor-qemu-20260920/full-set/serial.log.gz)
- [호스트 결과](evidence/health-monitor-qemu-20260920/host.json), [호스트 로그](evidence/health-monitor-qemu-20260920/host.log)
- [확장 시험 당시 소스 차이](evidence/health-monitor-qemu-20260920/validation-source-delta.patch), [그 SHA-256 목록](evidence/health-monitor-qemu-20260920/validation-source-manifest.json)
- [최종 소스 SHA-256](evidence/health-monitor-qemu-20260920/final-source-manifest.json), [최종 QEMU 소스 재현 패치](evidence/health-monitor-qemu-20260920/qemu-overlay.patch)
- [최초 보고서 보존본](evidence/health-monitor-qemu-20260920/report-before.md)

최종 소스 재현 패치는 clock commit `ceef13537`을 기준으로 만들었으며 클록 코드 자체와 보고서는 포함하지 않는다. 별도의 깨끗한 체크아웃에 적용하여 **최종 소스 67개 SHA-256 모두 일치**함을 확인했다. 여기에 `validation-source-delta.patch`와 당시 보고서를 적용하면 **확장 시험 당시 68개 SHA-256 모두 일치**한다. 확장 시험과 최종 상태의 차이는 시험 설정, 표적 제한 guard, 만료 시간 검사 실행기, 당시 보고서다.

재현 절차는 [QEMU 시험 README](../../os/kernel/health_monitor/tests/qemu/README.md)에 있다. QEMU의 보드·스토리지·공통 회귀 도구가 필요하므로 지정한 QEMU 커밋의 별도 체크아웃에 패치를 적용한다. 빌드 도구의 원래 경로와 결과 JSON의 `/private/tmp/...`는 당시 실행 환경 기록이며, 링크된 결과 파일·텍스트 로그·설정·소스 hash는 이 브랜치에 보관했다. 대용량 텍스트 로그는 gzip으로 보관하며 [원본 hash](evidence/health-monitor-qemu-20260920/compressed-logs.json)로 확인할 수 있다. ELF 바이너리는 커밋하지 않고 hash를 보관한다.

## 검증 경계

이 보고서의 QEMU-UP 검증표는 모두 완료했다. RTL8730E의 실제 SMP/캐시 순서, 실제 PM·절전·wakeup, HW watchdog fallback·리셋·reboot reason 저장, 보드 ISR 비용은 이 단일 코어 QEMU 장치로 검증할 수 없다. flat 빌드이므로 protected SVC ABI, 32비트 ARM이므로 64비트 unsigned-long 인자 거부 경로도 해당하지 않는다. 실제 보드와 원격 CI 통과를 주장하지 않는다.
