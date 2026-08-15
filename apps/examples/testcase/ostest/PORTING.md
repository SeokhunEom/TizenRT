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
- 이번 작업은 가져오기와 실행 가능 상태 확보가 목적이다. 실행 실패의 원인
  분석과 테스트 동작 변경은 범위에 포함하지 않는다.

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

## 검증 환경과 결과

- 빌드 컨테이너: `tizenrt/tizenrt:2.0.1-arm64-local`
  (`sha256:16b314997d3ed7901fe711ff85e1cd8e90885f44bb3d683fc7040699ebe2b74c`,
  `arm64`)
- 런타임: host `qemu-system-arm` 11.0.3
- 로컬 원문 로그: `build/ostest-port-results/`

빌드는 각 defconfig를 `configure.sh`로 적용하고 clean 상태에서 `make`했다.
QEMU runtime은 TASH prompt에서 `ostest`를 실행하고 최대 600초 동안 정상
종료, assertion, QEMU 종료를 관찰했다.

### 최종 결과

| 구성 | 빌드 결과 | 상세 |
| --- | --- | --- |
| `qemu-armv8m/hello` | 성공 | KERNEL 551,224 / 655,360 bytes (84.11%), size verification `PASS` |
| `qemu-armv8m/xip_all` | 성공 | KERNEL 236,612 / 655,360 bytes (36.1%), APP1 271 / 393,216 bytes (0.07%), COMMON 353,980 / 786,432 bytes (45.01%); 모두 `PASS` |
| `rtl8730e/loadable_ext_ddr_st7785` | 성공 | KERNEL 1,668,938 bytes, APP1 449 bytes, COMMON 946,256 bytes, RESOURCE 40,960 bytes; 모두 partition size verification `PASS` |

| QEMU 구성 | `ostest` 실행 결과 | 마지막 관찰 내용 |
| --- | --- | --- |
| `hello` | 600초 timeout, 실행기 종료 코드 124 | `setvbuf_test: Test NO buffering` 다음 정상 종료/assertion 없이 `RESULT ostest-timeout` |
| `xip_all` | A/B 부팅 및 common/app1 load 성공 후 600초 timeout, 실행기 종료 코드 124 | `setvbuf_test: Test NO buffering` 다음 정상 종료/assertion 없이 `RESULT ostest-timeout` |

두 QEMU 실행 모두 그 전에 argument test와 `vfork()` test를 진행했고,
`getopt`과 `memmem`은 의도한 `SKIP` 메시지를 출력했다. timeout의 원인
분석은 이번 포팅 범위에서 수행하지 않았다.

### 중간 실패 기록

`xip_all`의 첫 clean 빌드는 `prioinherit.c`의
`CONFIG_DEFAULT_TASK_STACKSIZE` 부재로 compile 실패했다. 제한된 호환
mapping을 시험한 두 번째 빌드는 같은 파일의 `gettid()` undefined reference로
link 실패했다. 이 증거에 따라 `prioinherit` 전체를 명시적 `SKIP`으로
전환했고, 이어진 clean 빌드는 성공했다. 원문은 각각
`xip_all-build-attempt1.log`, `xip_all-build-attempt2.log`에 보존했다.

최종 source 무결성 검증 결과는 다음과 같다.

```text
verified 53 imported .c/.h files
```
