# Health Monitor example

TASH에서 `health_monitor` 명령으로 정상 checkpoint, 감시 구간 반복, API 오류,
SMP CPU 이동과 의도적인 장애를 시험한다. 정상 명령은 `RESULT ... PASS/FAIL`을
출력하고, 장애 명령은 커널의 `HM fault=` 진단을 기대한다.

## 빌드 설정

```text
CONFIG_BUILD_FLAT=y
CONFIG_HEALTH_MONITOR=y
CONFIG_TASH=y
CONFIG_BUILTIN_APPS=y
CONFIG_EXAMPLES_HEALTH_MONITOR=y
```

Examples 메뉴에서 **Health Monitor example**을 활성화한다. `apps/examples`의
기존 wildcard Make.defs와 Kconfig 생성 경로로 발견되며, Makefile은
`health_monitor_main`을 `TASH_EXECMD_ASYNC`로 등록한다. 명령마다 별도 태스크에서
계약을 시작하므로 shell 태스크 자체를 감시 대상으로 삼지 않는다.

Health Monitor는 현재 사용자 공간 syscall/device ABI가 없어 이 example은
**flat build 전용**이다. Protected/kernel/loadable 앱에서 직접 호출하는 구성은
Kconfig와 소스 guard로 차단한다. 별도 task에서 실행하는 test entrypoint이며,
이미 감시 중인 태스크에서 `health_monitor_main()`을 직접 호출하지 않는다.

Core의 제한도 적용된다: 실제 주기 tick이 필요하고, tickless/tick suppression,
fake tick, APP_BINARY_SEPARATION은 지원하지 않는다. SMP는 현재 RTL8730E의
amebasmart port에 한정된다. 기존 `rtl8730e/loadable_ext_ddr_st7785`에 example
옵션만 켜는 것으로 실행 가능한 것은 아니며, 지원되는 flat 구성과 보드 빌드가
먼저 필요하다. 이 변경에서는 defconfig나 자동 부팅 등록을 바꾸지 않았다.

## 실행

인자 없이 실행하면 도움말만 출력한다. Fatal 시험은 자동으로 실행하지 않는다.

| 명령 | 시험 내용 | 기대 결과 |
| --- | --- | --- |
| `health_monitor healthy 5 2000` | 한 계약을 유지하며 작업·대기·kick을 5회 수행하고 stop | `RESULT health_monitor healthy PASS completed=5 ret=0` |
| `health_monitor restart 5 2000` | start→작업→kick→stop과 감시 밖 대기를 5회 반복 | `RESULT health_monitor restart PASS completed=5 ret=0` |
| `health_monitor errors` | inactive kick/stop, start(0), duplicate start, 정상 kick/stop, 중복 stop 검사 | `RESULT health_monitor errors PASS completed=8 ret=0` |
| `health_monitor migrate 6 2000` | active 계약을 유지하며 CPU affinity를 순회하고 실제 CPU 번호 확인 | `RESULT health_monitor migrate PASS completed=6 ret=0` |
| `health_monitor timeout 2000` | start 후 kick/stop 없이 대기 | `EXPECT_FATAL ... reason=1` 다음 커널 `HM fault=...0001` |
| `health_monitor exit 2000` | start 후 stop 없이 명령 태스크 종료 | `EXPECT_FATAL ... reason=2` 다음 커널 `HM fault=...0002` |

`healthy/restart/migrate`는 cycles와 timeout_ms를 생략할 수 있다. 기본값은 각각
5회, 2000ms다. Cycles는 1..1000, timeout은 100..60000ms이며 최소 8 system tick을
포함해야 한다. Migrate는 CPU가 둘 이상인 SMP와 cycles >= 2가 필요하다. 잘못된
명령이나 범위는 계약을 시작하지 않고 실패한다.

정상 작업은 작은 계산 batch와 timeout의 1/4에 해당하는 대기로 구성된다.
Kick은 그 batch 이후에만 수행한다. Restart는 stop 후 같은 길이만큼 감시 밖에서
대기한다. 장시간 console I/O가 정상 시험의 timeout 원인이 되지 않도록 진행 로그는
계약 시작 전, 결과 로그는 cleanup 후 출력한다. CPU 이동 시험의 affinity API는
기존 scheduler 동작을 사용하며, Health Monitor 자체의 non-blocking 성질과는 별개다.
Migrate는 stop 후 원래 affinity를 복원한다.

TASH가 prompt를 다시 출력하는 것만으로 시험이 끝난 것은 아니다. 명령은 비동기
태스크이므로 위 `RESULT` 또는 커널 fault 기록을 기다린다. 여러 정상 명령을 동시에
실행할 수 있지만 첫 확인은 하나씩 수행하는 편이 결과를 구분하기 쉽다.

## 오류와 의도적인 장애

`-EAGAIN`과 `-ENOSPC`만 최대 3번까지 재시도하며, 사이에 한 tick 이상에 해당하는
시간을 sleep한다. 성공할 때까지 spin하지 않는다. 다른 API 오류는 즉시 실패
처리하고 가능한 경우 stop한다. Cleanup stop도 실패하면 계약은 여전히 active이며
`FAIL`을 출력한다. 이 태스크의 종료가 unexpected-exit 진단으로 이어질 수 있다.

`timeout`과 `exit`는 **시스템의 fatal 진단/panic을 의도적으로 유발**한다. 별도
시험 부팅에서 각각 실행하고 로그를 수집한다. 명령이 출력한 `EXPECT_FATAL`은
기대하는 결과일 뿐이며, 실제 커널의 reason/PID/slot 기록을 확인해야 한다. Start에
실패하면 `FAIL`을 출력한다. Fatal mode에는 `PASS` 출력이 없다. 특히 exit 명령의
태스크 반환값 0은 모니터가 장애를 감지했다는 증거가 아니다.

Core가 HW watchdog을 소유하지 않으므로 CPU0 tick이나 상세 진단이 멈추면 reset이
보장되지 않는다. 예상 reason의 `HM fault=` 기록, 상세 dump, panic/reset 관측을
구분해서 기록한다. 자세한 한계는 [core specification](../../../docs/health_monitor.md)을
참고한다.

## 개발 환경 검증

```sh
python3 tests/health_monitor/run_example.py
python3 tests/health_monitor/compile_arm.py
```

첫 명령은 실제 example C와 core C를 fake tick/IRQ/affinity/UART/panic에 연결해
UP/SMP 정상·오류·bounded retry·cleanup·fatal 경로를 검사한다. 실제 example Makefile과
REGISTER macro로 임시 디렉터리에 TASH 등록 파일도 생성해 async 등록을 확인한다.
ASan/UBSan을 사용하며 실제 보드를 panic시키는 시험은 아니다.

두 번째 명령은 실제 ARM 헤더로 Cortex-M3 UP와 Cortex-A32 SMP core 및 flat example
object를 컴파일한다. 원래 defconfig 전체의 link/boot나 실기기 TASH 실행 증거는 아니다.
신규 example의 전체 firmware 빌드·실기기 실행은 아직 검증하지 않았다.
