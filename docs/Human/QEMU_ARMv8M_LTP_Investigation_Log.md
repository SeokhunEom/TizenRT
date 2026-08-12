# QEMU ARMv8-M LTP 이슈 분석 및 해결 기록

## 작업 계약

- 기준 브랜치: `codex/qemu-armv8m-kernel-tc` (`b5eacbdc0`)
- 작업 worktree: `/Volumes/T7/Dev/TizenRT/codex/qemu-armv8m-ltp`
- 작업 브랜치: `codex/qemu-armv8m-ltp`
- 가져온 커밋: `2ec68830a2283dad9ca96ce2c6ad052fb2404152`
- cherry-pick 결과: `33473b7ba`
- 대상: `qemu-system-arm -M mps2-an505`에서 `qemu-armv8m/hello` LTP

### 범위와 완료 기준

1. 외부 커밋이 선택한 LTP 테스트를 QEMU ARMv8-M flat build에 통합한다.
2. 명령별 원본 소스와 실제 종료 상태를 기계적으로 추적한다.
3. 최초 실패를 재현하고 원인을 구현 또는 포팅 경계까지 좁힌다.
4. 필요한 최소 수정 후 등록된 전체 테스트를 새 QEMU 한 세션에서 실행한다.
5. 최종 기준은 PASS 46, FAIL/timeout/crash/assertion 0이며 기존
   `network_tc`와 `kernel_tc` 회귀도 통과해야 한다.

### 비목표

- 외부 커밋이 선택하지 않은 LTP 카테고리로 범위를 확대하지 않는다.
- TizenRT가 지원하지 않는 POSIX 기능을 테스트 전용 허위 구현으로 추가하지 않는다.
- QEMU 결과를 BK7239N 등 실제 하드웨어 검증으로 간주하지 않는다.
- 관련 없는 QEMU recipe나 기존 test case를 리팩터링하지 않는다.

## 환경과 기준 상태

검증일은 2026-08-12 KST이다.

- Docker Server: `29.6.2`, `aarch64`
- Docker image: `tizenrt/tizenrt:2.0.1-arm64-local`
- image ID: `sha256:16b314997d3ed7901fe711ff85e1cd8e90885f44bb3d683fc7040699ebe2b74c`
- host QEMU: `11.0.3`
- QEMU machine/CPU: `mps2-an505` / Cortex-M33
- 시작 시 기준 worktree는 clean이고 local/remote HEAD가 `b5eacbdc0`으로 같았다.
- 외부 커밋은 새 worktree에 conflict 없이 적용됐다.

worktree 생성 직후 첫 cherry-pick 명령의 실행 디렉터리가 기존 `main`인 것을
감지했다. 새 worktree에 올바르게 다시 적용한 뒤, 기존 `main`은 사전 확인한
clean `origin/main` (`080481ee8`)으로 되돌리고 SHA와 clean 상태를 확인했다.
`git reset --hard`는 사용하지 않았다.

## 외부 커밋 정적 분석

가져온 커밋은 LTP 20230516 archive, 16개 호환 patch, 빌드/등록 script를
추가했지만 다음 실행 경계가 비어 있었다.

- 어느 QEMU defconfig에서도 `CONFIG_EXAMPLES_LTP`가 활성화되지 않았다.
- TASH 명령은 async callback이라 명령 수락만 보이고 callback 반환값은 host가
  알 수 없었다. 출력에 `Test PASSED`가 없는 정상 테스트도 있어 문자열 검색만으로
  판정할 수도 없었다.
- `ltp_tN`과 원본 source의 영속적인 manifest가 없었다.
- zip이 보존한 2023년 directory timestamp가 2026년 archive 파일보다 오래되어
  `make context`마다 LTP 전체를 다시 풀었다.
- patch loop의 `|| true`가 patch 적용 실패도 성공처럼 진행시켰다.
- README는 4개 test와 12개 patch만 설명해 실제 46개/16개 상태와 달랐다.

별도 recipe를 복제하지 않고 pthread, builtin TASH, `CONFIG_RR_INTERVAL=10`을
이미 가진 `qemu-armv8m/hello`에 다음만 추가했다.

```text
CONFIG_EXAMPLES_LTP=y
CONFIG_EXAMPLES_LTP_STACKSIZE=8192
CONFIG_EXAMPLES_LTP_PRIORITY=100
```

## 관찰 가능한 실행 경로 구성

`ltp_register.sh`가 build 때 다음 두 파일을 생성하도록 했다.

- `ltp_registry.inc`: `ltp_tN`에서 rename된 LTP entry로 가는 table
- `ltp_manifest.tsv`: command, C entry, 원본 source의 정확한 매핑

모든 TASH 명령은 `ltp_runner_main`으로 들어간다. wrapper는 선택한 LTP entry를
child task로 만들고 `waitpid()`로 종료 상태를 회수한 뒤 아래 한 줄을 출력한다.

```text
LTP_RESULT ltp_t1 PASS exit=0
```

host runner `.github/scripts/qemu-armv8m-ltp.py`는 registry와 manifest가 정확히
일치하고 index가 연속인지 확인하고 QEMU를 시작한다. 각 명령은 완전한 newline
종료 결과만 판정하며, partial serial chunk의 `P` 또는 `PA`를 PASS로 오인하지
않는다. raw serial log와 source mapping을 포함한 JSON을 함께 남기고 crash 또는
timeout이면 즉시 중단한다.

## 최초 실행 결과

초기 46개를 두 새 QEMU 세션(`t1`-`t20`, `t21`-`t46`)으로 분할해 얻은 합산
결과는 PASS 37, FAIL 8, timeout 1이었다.

| 테스트 | 최초 증상 | 분류 |
| --- | --- | --- |
| `ltp_t1`, `ltp_t2` | `high priority was not woken up` | FAIL |
| `ltp_t3` | `High priority was not woken up` | FAIL |
| `ltp_t9`, `ltp_t10` | 범위 밖 priority를 설정해도 API가 성공 | FAIL |
| `ltp_t20` | 출력 없이 30초/45초 timeout | HANG |
| `ltp_t24`, `ltp_t27` | invalid policy에서 `errno == 0` | FAIL |
| `ltp_t31` | unknown pid에서 `errno == 0` | UNRESOLVED |

나머지 37개는 최초 실행부터 PASS였다.

## 원인 분석과 수정

### 1. `ltp_t1`-`ltp_t3`: default pthread priority 가정

upstream test는 main을 priority 5로 낮춘 뒤 `pthread_create(..., NULL, ...)`로
만든 low thread가 그 scheduling을 상속한다고 가정한다. TizenRT의
`PTHREAD_ATTR_INITIALIZER`는 `PTHREAD_EXPLICIT_SCHED`, priority 100이다. 따라서
의도한 low thread가 실제로는 priority 100이 되어 priority 10의 high thread를
선점했고 wake-up 순서가 반전됐다. 명시적 RR priority 5 attribute로 low thread를
만드는 `0017` patch를 추가했다. 커널의 TizenRT 기본값은 바꾸지 않았다.

### 2. `ltp_t9`, `ltp_t10`: priority 범위 검증 누락

`pthread_attr_setschedparam()`은 pointer만 검사하고 priority를 `short`로 저장했다.
`SCHED_PRIORITY_MIN..SCHED_PRIORITY_MAX` 밖이면 POSIX error code `EINVAL`을
반환하도록 validation을 추가했다.

### 3. `ltp_t24`, `ltp_t27`: invalid policy의 errno 누락

`sched_get_priority_max()`와 `sched_get_priority_min()`은 invalid policy에서
`ERROR`만 반환했다. 두 함수 모두 `errno=EINVAL`을 설정하도록 수정했다.

### 4. `ltp_t31`: unknown pid의 errno 누락

`sched_getparam()`은 null output 또는 존재하지 않는 pid에서 errno 없이
`ERROR`를 반환했다. null output은 `EINVAL`, unknown pid는 `ESRCH`로 구분했다.

### 5. `ltp_t20`: signal 소유권과 priority starvation

이 test는 main에 signal handler를 등록한 뒤 worker에서 signal을 unmask하는
Linux process-wide disposition을 가정했다. TizenRT `sigaction()`은 현재 TCB의
action queue를 갱신하므로 실제 signal 수신 worker에도 SIGUSR1/SIGUSR2 handler를
등록하고 readiness semaphore로 sender 시작 전에 완료를 보장했다.

첫 수정 후에도 timeout이 남았다. worker가 반복 호출하는
`pthread_setschedparam()`은 main을 priority 1로 낮추지만, TizenRT default로 만든
worker/sender는 priority 100이었다. 항상 runnable인 worker가 main의 sleep 복귀를
영구적으로 막은 것이 두 번째 원인이었다. test가 의도한 동일 scheduling 관계를
보존하도록 worker와 두 sender를 명시적 `SCHED_RR`, priority 1로 생성했다.
수정 후 `ltp_t20`은 1.6초 안에 종료했다.

### 6. build 재현성

- unzip 출력은 quiet mode로 제한했다.
- 모든 patch는 `set -e` 아래에서 적용해 하나라도 실패하면 build가 실패한다.
- patch 완료 후 extraction directory를 touch해 매 context 재압축 해제를 막았다.
- generated source/manifest와 local runtime artifact만 정확한 경로로 ignore했다.

## 반복 검증 기록

| 반복 | 결과 | 판단 및 다음 수정 |
| --- | --- | --- |
| 0 | LTP 비활성 | hello defconfig에 LTP 3개 option 추가 |
| 1 | build 성공, 46개 등록 | async TASH 반환값을 host에서 판정할 수 없음 |
| 2 | PASS 37, 실패 9 | 종료 wrapper/manifest/runner로 실패 집합 확정 |
| 3 | `t1`-`t3`, `t9`, `t10`, `t24`, `t27`, `t31` PASS, `t20` timeout | signal handler만으로는 부족, priority starvation 확인 |
| 4 | `t20` PASS (1.6초) | 모든 최초 실패 해소 |
| 5 | 전체 46/46 PASS | 기존 network/kernel 회귀 실행 |
| 6 | clean build 후 전체 46/46 PASS | patch strict 적용과 최종 산출물 재검증 |

crash PC나 assertion address는 발생하지 않아 `addr2line`은 필요하지 않았다.
진단 중 생성한 중간 serial/JSON은 최종 검증 후 삭제했고, hash를 기록한 최종 clean
LTP/회귀 산출물 4개만 local build artifact로 남겼다. extracted LTP tree는 증분 build
입력이라 Git에서만 제외한다. 임시 debug print는 최종 source 또는 patch에 남아 있지
않다.

## 최종 결론과 검증 경계

최종 clean build는 18개 patch를 모두 적용하고 46개 명령을 등록했으며 kernel
partition 538,400/655,360 bytes(82.15%)로 성공했다. 전체 LTP 결과와 artifact
hash, 기존 회귀 수치는 `QEMU_ARMv8M_LTP_Test_Results.md`에 기록한다.

검증된 범위는 Apple Silicon host의 ARM64 Docker build와 QEMU Cortex-M33
software model이다. 실제 board timing, peripheral, toolchain/host가 다른 환경은
검증하지 않았다.
