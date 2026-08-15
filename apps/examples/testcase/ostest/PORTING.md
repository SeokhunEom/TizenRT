# Apache NuttX `ostest` TizenRT 포팅 기록

## 범위와 원칙

- 기준 브랜치/커밋: `codex/qemu-armv8m-kernel-tc`
  (`b5eacbdc0426df9ca0784dc5198ac8ff30443631`)
- 작업 브랜치: `codex/nuttx-ostest-port`
- upstream: `apache/nuttx-apps`의 `testing/ostest`
  (`0faa02f29f28c8b0c60a79726399fc477808864b`,
  `nshlib: apply #/$ prompt markers after login`)
- 포팅 대상은 upstream의 `.c`/`.h` 53개 전체다. 테스트 로직은 수정하지
  않고, TizenRT에 없는 기능은 별도 어댑터에서 명시적으로 `SKIP`한다.
- 포팅을 우선하며 테스트 본문과 TizenRT 공용 구현은 변경하지 않는다.
  실행을 막는 실패는 재현 로그와 구현 비교로 경계를 확인한 뒤 별도
  어댑터에서 `SKIP`하여 나머지 테스트를 계속 실행한다.

## 원본 보존 경계

upstream 53개 파일에 허용한 차이는 다음 include 치환뿐이다.

| upstream | TizenRT | 이유 |
| --- | --- | --- |
| `<nuttx/...>` | `<tinyara/...>` | TizenRT 헤더 namespace |
| `<nuttx/debug.h>` | `<debug.h>` | TizenRT debug 헤더 위치 |
| `<malloc.h>` | `<stdlib.h>` | TizenRT가 사용하는 newlib 헤더와의 `wint_t`/`mallinfo` 중복 선언 회피 |

위 치환을 upstream 원본에도 동일하게 적용한 뒤 대상 파일과 `diff`하여
53/53 파일이 일치함을 확인했다. `ostest_entry.c`, `ostest_sighelper.c`,
`ostest_unsupported.c`는 원본을 바꾸지 않기 위해 추가한 TizenRT 전용
어댑터이므로 이 비교 대상에서 제외한다.

이 검증은 다음처럼 반복할 수 있다.

```sh
apps/examples/testcase/ostest/verify_upstream.sh \
  /path/to/nuttx-apps/testing/ostest
```

## 빌드와 진입점 통합

- `apps/examples/testcase/Kconfig`에서 `ostest/Kconfig`를 읽는다.
- testcase 상위 `Makefile`이 1단계 하위의 `Make.defs`도 수집하도록 하고,
  `ostest_main`을 비동기 TASH 명령 `ostest`로 등록한다.
- upstream의 `main()`은 직접 수정하지 않는다. `ostest_entry.c`가 전처리기
  alias를 통해 `ostest_main()`으로 연결한다.
- QEMU `hello`, `xip_all` defconfig에는 `CONFIG_TESTING_OSTEST=y`,
  `CONFIG_TESTING_OSTEST_STACKSIZE=8192`,
  `CONFIG_TESTING_OSTEST_WAITRESULT=y`를 명시했다. TizenRT의
  `configure.sh`는 새 Kconfig default를 기존 defconfig에 자동으로
  기록하지 않으므로, 명령 task가 실제 테스트 종료까지 기다리도록 이 값을
  명시해야 했다.

## TizenRT 전용 어댑터와 비활성 테스트

| 항목 | 처리 | 포팅 판단 |
| --- | --- | --- |
| `sighelper.c` | `ostest_sighelper.c` 사용 | NuttX는 배열 기반 `sigset_t` 내부 표현을 참조하지만 TizenRT는 32-bit scalar다. 공개 값의 동등 비교만 제공한다. |
| `prioinherit.c` | `SKIP` | NuttX 전용 stack-size 설정과 `gettid()`에 의존한다. |
| `getopt.c` | `SKIP` | TizenRT libc에 `getopt_long()`/`getopt_long_only()`가 없다. |
| `libc_memmem.c` | `SKIP` | TizenRT libc에 `memmem()`이 없다. |
| `setvbuf.c` | `SKIP` | TizenRT `lib_fwrite()`가 `_IONBF` 스트림에 쓸 때 진행하지 못한다. 아래 재현과 구현 비교를 참고한다. |
| `sigprocmask.c` | `SKIP` | 테스트가 요구하는 POSIX signal 번호를 TizenRT 설정이 제공하지 않는다. |
| `timedmutex.c` | `SKIP` | `pthread_mutex_timedlock()`을 제공하지 않는다. |
| `timedwait.c` | `SKIP` | `gettid()`을 제공하지 않는다. |
| `spinlock.c` | `SKIP` | NuttX atomic/seqlock 공개 API가 TizenRT에 없다. |
| `wdog.c` | `SKIP` | NuttX 내부 watchdog API가 TizenRT API와 호환되지 않는다. |
| `wqueue.c` | `SKIP` | NuttX 동적 work queue 생성 API가 TizenRT에 없다. |
| `roundrobin.c` | `SKIP` | 테스트가 NuttX atomic 공개 API에 의존한다. |

그 밖의 테스트는 upstream 조건과 대응하는 TizenRT 설정이 켜졌을 때만
`Make.defs`에서 빌드한다. 현재 대상 설정에서 노출하지 않는 upstream 기능을
위해 새 추상화나 호환 API를 만들지는 않았다.

## 포팅 중 확인한 문제와 판단

1. `<malloc.h>`가 toolchain newlib 헤더로 해석되어 TizenRT 선언과 충돌했다.
   테스트 본문 대신 표준 선언을 제공하는 `<stdlib.h>` include로 바꿨다.
2. upstream `sighelper.c`는 `sigset_t` 내부 필드에 의존했다. 원본은 보존하고
   TizenRT 표현에 맞춘 작은 동등 비교 어댑터로 교체했다.
3. 초기 link에서 TizenRT가 제공하지 않는 심볼이 확인됐다. 해당 테스트의
   원본 파일은 그대로 두고 빌드 대상에서 제외했으며, 호출 위치를 바꾸지
   않기 위해 이름이 같은 `SKIP` 함수를 별도 파일에 구현했다.
4. source 목록을 줄인 뒤 증분 빌드의 `libapps.a`에 이전 object가 남았다.
   최종 검증은 `make clean` 이후 다시 수행하여 source 변경과 archive 잔존
   효과를 분리했다.
5. `xip_all`은 priority inheritance를 켜므로 upstream이 기대하는
   `CONFIG_DEFAULT_TASK_STACKSIZE`가 없으면 compile이 중단됐다. stack-size
   호환 mapping으로 compile을 통과시킨 뒤에도 TizenRT에 없는 `gettid()`
   link 실패가 확인되어, 해당 원본 테스트 전체를 `SKIP` 처리했다.

## `setvbuf` 정지 현상 분류

결론은 QEMU ARMv8-M 커널, scheduler 또는 architecture 문제가 아니라
**TizenRT libc stdio의 unbuffered write 구현 결함**이다. NuttX와 TizenRT의
구현 차이가 이 문제를 드러냈으며, upstream 테스트의 기대 동작은 POSIX
`setvbuf()` 사용 범위 안에 있다.

원본 테스트를 수정하지 않고 임시 진단 wrapper로 `fopen()`, `setvbuf()`,
`fprintf()` 호출 전후만 기록했다. wrapper는 증거를 얻은 뒤 삭제했다.
`hello-setvbuf-diagnostic-runtime.log`의 결과는 다음과 같다.

```text
setvbuf_test: Test NO buffering
OSTEST_DIAG: before fopen
OSTEST_DIAG: after fopen stream=0x800373f4
OSTEST_DIAG: before setvbuf
OSTEST_DIAG: after setvbuf ret=0
OSTEST_DIAG: before fprintf
RESULT reached-before-fprintf
RESULT fprintf-timeout
```

따라서 `/dev/console` open과 `_IONBF` mode 설정은 성공하고, 첫 unbuffered
`fprintf()`가 반환하지 않는 경계를 실행으로 확인했다.

구현 비교 결과는 다음과 같다.

1. TizenRT `lib/libc/stdio/lib_setvbuf.c`는 `_IONBF`에서 buffer와 size를
   각각 `NULL`, `0`으로 만든 뒤 `fs_bufstart`, `fs_bufpos`, `fs_bufend`를
   zero-capacity 상태로 설정한다.
2. TizenRT `lib/libc/stdio/lib_libfwrite.c`는 이 상태도 buffered path로
   처리한다. loop의 `gulp_size = fs_bufend - fs_bufpos`는 계속 0이고,
   `count -= gulp_size`가 입력 길이를 줄이지 못하므로 write loop가 끝나지
   않는다.
3. 비교한 Apache NuttX kernel commit
   `36a971567ac706b86fb9e94cceeb3c81083da344`의 `lib_libfwrite.c`는
   `fs_bufstart == NULL`을 먼저 검사하여 I/O write 함수로 즉시 전달한다.

이 문제는 TizenRT 공용 libc 수정으로 해결할 수 있지만, 이번 요청은 포팅과
후속 테스트 진행이 우선이므로 공용 구현은 바꾸지 않았다. `setvbuf.c` 원본을
source list에서 제외하고 `ostest_unsupported.c`에 같은 이름의 명시적
`SKIP` entry만 추가했다.

## 검증 환경과 결과

- 빌드 컨테이너: `tizenrt/tizenrt:2.0.1-arm64-local`
  (`sha256:16b314997d3ed7901fe711ff85e1cd8e90885f44bb3d683fc7040699ebe2b74c`,
  `arm64`)
- 런타임: host `qemu-system-arm` 11.0.3
- 로컬 원문 로그: `build/ostest-port-results/`

빌드는 각 defconfig를 `configure.sh`로 적용하고 clean 상태에서 `make`했다.
QEMU runtime은 TASH prompt에서 `ostest`를 실행하고 정상 종료, assertion,
QEMU 종료를 관찰했다. 실행한 test가 다음 memory checkpoint 또는 suite
종료까지 도달하고 `ERROR`, assertion이 없을 때 `완료`로 기록했다. 개별
test가 자체 PASS 집계를 제공하지 않는 경우가 있으므로 `완료`를 더 강한
의미의 독립 pass assertion으로 해석하지 않는다.

### 최종 결과

| 구성 | 빌드 결과 | 상세 |
| --- | --- | --- |
| `qemu-armv8m/hello` | 성공 | KERNEL 549,208 / 655,360 bytes (83.8%), size verification `PASS` |
| `qemu-armv8m/xip_all` | 성공 | KERNEL 236,616 / 655,360 bytes (36.1%), APP1 271 / 393,216 bytes (0.07%), COMMON 351,918 / 786,432 bytes (44.75%); 모두 `PASS` |
| `rtl8730e/loadable_ext_ddr_st7785` | 성공 | KERNEL 1,668,938 bytes, APP1 449 bytes, COMMON 946,256 bytes, RESOURCE 40,960 bytes; 모두 partition size verification `PASS` |

| QEMU 구성 | `ostest` 실행 결과 | 마지막 관찰 내용 |
| --- | --- | --- |
| `hello` | 성공, status 0 | `user_main: Exiting`, `ostest_main: Exiting with status 0`, `RESULT ostest-complete` |
| `xip_all` | 성공, status 0 | A/B boot와 common/app1 load 후 `user_main: Exiting`, `ostest_main: Exiting with status 0`, `RESULT ostest-complete` |

### 성공적으로 완료한 테스트

아래는 최종 runtime에서 실제 호출되어 다음 checkpoint까지 완료된 항목이다.

| 테스트 | `hello` | `xip_all` |
| --- | --- | --- |
| stdio startup, environment/libc environment operations | 완료 | 완료 |
| `vfork`, argument 전달 | 완료 | 완료 |
| `/dev/null`, `task_restart` | 완료 | 완료 |
| `waitpid`, `waitid`, `wait` | 완료 | 완료 |
| mutex, moved mutex, recursive mutex | 완료 | 완료 |
| pthread cancellation | 완료 | 완료 |
| semaphore, timed semaphore | 완료 | 완료 |
| condition variable | 완료 | upstream test 자체 SKIP: priority inheritance와 test logic이 호환되지 않음 |
| `pthread_exit` | 완료 | 완료 |
| pthread rwlock, rwlock cancellation | 완료 | 완료 |
| timed message queue, message queue | 완료 | 완료 |
| pthread barrier | 완료 | 완료 |
| scheduler lock | 완료 | 완료 |

### 실패 및 SKIP 테스트

포팅 후 실제 실행 실패로 확인한 항목은 `setvbuf` 하나다. 두 config의 첫
실행은 모두 `setvbuf_test: Test NO buffering` 뒤 600초 timeout이 발생했다.
위 진단 실행에서 첫 `fprintf()` 내부 정지로 경계를 좁혔고, 원인 분류 후
최종 실행에서는 SKIP했다. `hello-ostest-final.log`,
`xip_all-ostest-final.log`, `hello-setvbuf-diagnostic-runtime.log`에 원문이
남아 있다. `setvbuf`를 SKIP한 뒤 추가 failure, assertion 또는 timeout은
관찰되지 않았다.

최종 runtime에서 출력된 포팅 어댑터 SKIP은 다음과 같다.

| 테스트 | `hello` | `xip_all` | 이유 |
| --- | --- | --- | --- |
| `getopt` | SKIP | SKIP | `getopt_long` 계열 API 없음 |
| `memmem` | SKIP | SKIP | `memmem()` 없음 |
| `setvbuf` | SKIP | SKIP | `_IONBF` stream write가 `lib_fwrite()`에서 정지 |
| timed mutex | SKIP | SKIP | `pthread_mutex_timedlock()` 없음 |
| timed wait | SKIP | SKIP | `gettid()` 없음 |
| `sigprocmask` | SKIP | SKIP | 필요한 POSIX signal 번호 없음 |
| round-robin | SKIP | SKIP | NuttX atomic 공개 API 의존 |
| work queue | SKIP | config 조건상 미실행 | 동적 work queue 생성 API 없음 |
| spinlock | SKIP | config 조건상 미실행 | NuttX atomic/seqlock 공개 API 없음 |
| watchdog | SKIP | config 조건상 미실행 | NuttX 내부 watchdog API와 비호환 |
| priority inheritance | config 조건상 미실행 | SKIP | `gettid()` 없음 |

최종 전체 실행 로그는 `hello-iteration1-runtime.log`과
`xip_all-after-skips-runtime.log`에 보존했다.

### 중간 실패 기록

`xip_all`의 첫 clean 빌드는 `prioinherit.c`의
`CONFIG_DEFAULT_TASK_STACKSIZE` 부재로 compile 실패했다. 제한된 호환
mapping을 시험한 두 번째 빌드는 같은 파일의 `gettid()` undefined reference로
link 실패했다. 이 증거에 따라 `prioinherit` 전체를 명시적 `SKIP`으로
전환했고, 이어진 clean 빌드는 성공했다. 원문은 각각
`xip_all-build-attempt1.log`, `xip_all-build-attempt2.log`에 보존했다.

`setvbuf` 진단용 `hello` 첫 build는 top-level 병렬 `make`의 post-size
검사가 `tinyara.bin` 생성보다 먼저 실행되어 실패했다. compiler/linker
오류는 없었고 직렬 `make` 재시도는 성공했다. 각각
`hello-setvbuf-diagnostic-build.log`,
`hello-setvbuf-diagnostic-build-retry.log`에 남겼다. 이는 ostest test
실패와 별개인 build 순서 경합이다.

최종 source 무결성 검증 결과는 다음과 같다.

```text
verified 53 imported .c/.h files
```
