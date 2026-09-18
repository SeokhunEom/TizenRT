# QEMU build_test: condition timeout 정지 수정

검증일: 2026-09-18 KST. 브랜치: `codex/qemu-build-test`.
[2단계 baseline](QEMU_Build_Test_Stage02.md)은 먼저 `014415d57ea3ac3345f407696799187c174ec955`로 커밋했다.
이 문서는 그 뒤 진행한 정지 재현, 원인 확인 및 수정 검증을 기록한다.

## 확인된 원인

기존 전체 `kernel_tc`는 206개 PASS 뒤 timed rwlock 검사에서 끝나지 않았다. GDB에서 kernel 테스트와 두 pthread는 WAITSEM 상태였지만 TASH와 OS tick은 동작했다.

`build/configs/qemu/build_test/defconfig`는 `CONFIG_MAX_WDOGPARMS=2`다. 그런데 `os/kernel/pthread/pthread_condtimedwait.c`는 watchdog에 `(pid, SIGCONDTIMEDOUT, cond)` 세 인자를 전달했다. `os/kernel/wdog/wd_start.c`는 인자 수가 설정값보다 크면 `EINVAL`/`ERROR`를 반환한다. 호출부가 이를 무시하고 `sem_wait()`에 들어가므로, timeout watchdog 없이 조건을 기다렸다. timed rwlock의 두 스레드가 이 경로에 들어가며 상위 테스트의 join도 반환하지 않았다.

세 번째 인자는 `60e94b2224e1fbb0bbd97793ff07efcde23b8467`에서 추가됐다. 해당 변경은 condition waiter 감소를 timeout callback으로 옮겼다. 이번 수정은 그 동작을 유지한다.

## 수정

- callback에는 `(pid, cond)` 두 인자만 전달하고, 항상 같은 timeout signal은 callback 내부에서 `SIGCONDTIMEDOUT` 상수로 사용한다.
- `wd_start()` 실패 시 errno를 보존하고 semaphore 대기를 시작하지 않는다. 기존 공통 경로에서 mutex를 다시 획득하고 watchdog을 정리한 뒤 오류를 반환한다.
- `tc_libc_pthread.c`에 공개 API 회귀 테스트를 추가했다. 같은 condition/mutex에 대해 신호 없이 두 번 timed wait하여 `ETIMEDOUT`, mutex unlock, 최종 destroy 성공을 확인한다.

원래 `build_test/defconfig`는 바꾸지 않는다. 최종 `.config`도 해당 defconfig와 byte 단위로 동일하게 복원한다. 인자 한도를 늘려 우회하지 않아 watchdog 객체 크기도 늘어나지 않는다.

회귀 테스트의 기대 동작은 [POSIX pthread_cond_timedwait](https://pubs.opengroup.org/onlinepubs/009604599/functions/pthread_cond_timedwait.html)의 timeout 반환과 mutex 재획득 계약을 따른다.

## 대조 실험과 회귀 검증

최소 구성은 기존 OS 기능을 유지하면서 TC 선택만 libc pthread로 줄였다. 진단용 구성은 ignored `tmp/`에 보존하며 제품 defconfig에는 반영하지 않는다. 각 실행은 새 QEMU에서 수행했다.

| 실행 | watchdog 인자 한도 | OS 소스 / 테스트 | 결과 |
|---|---:|---|---|
| `minimal-red` | 2 | 수정 전 / 기존 pthread TC | 30 PASS 출력 후 40초 timeout, 최종 요약 없음 |
| `capacity3-control` | 3 | 동일 수정 전 소스 / 기존 pthread TC | PASS 32 / FAIL 0, 16.497초 |
| `regression-red` | 2 | 수정 전 / 새 직접 timeout TC 포함 | 새 TC에서 15초 timeout, 최종 요약 없음 |
| `minimal-fixed` | 2 | 수정 후 / 새 TC 포함 | PASS 33 / FAIL 0, 17.162초 |
| `start-error-recovery` | 1 | 수정 후 / 등록 실패 경로 전용 진단 | PASS 1 / FAIL 0, 0.202초 |

`start-error-recovery`는 watchdog이 두 인자를 거부하도록 임시 `.config`를 1로 바꾸고, 새 TC만 실행하면서 기대값을 `EINVAL`로 바꾼 실험이다. 같은 condition을 두 번 호출하고 mutex unlock/destroy까지 성공했다. 진단용 테스트 소스는 `capacity1-test.c`로 보존했으며 실행 후 실제 소스와 최소 구성을 복원했다. 이 구성은 정상 기능 구성으로 권장하지 않는다.

시간은 호스트 경과 시간이다. guest tick과 호스트 시계 속도는 같지 않다. timeout 실행의 부분 PASS 수는 suite 최종 집계가 아니다.

## 원래 전체 구성 검증

원래 defconfig 그대로 clean build에 성공했다. 새 부팅에서 `help`, `ps`, `free`가 정상 응답했다.

전체 `kernel_tc`는 **513.133초 후 `Kernel TC End [PASS : 431, FAIL : 1]`**로 끝났다. 새 직접 timeout 회귀 TC, 기존 timed rwlock TC, 기존 `tc_pthread_pthread_timed_wait`가 모두 통과했다. 종료 후 `ps`에서 kernel TC 및 검사 pthread가 사라졌고 `free`도 응답했다. 기존 정지는 해소됐지만, 아래 기존 TC 기대값 불일치가 남아 runner의 전체 판정과 종료 코드는 실패(1)다.

- ELF SHA-256: `cf445022050eae334571092976e46be275832bc2365486f3be1aeb13bbd8d740`
- BIN SHA-256: `3aa8b8116fe372da84672f1d5129122d457a9d697875fbd115aea516af82c50a`
- `.config` 및 defconfig SHA-256: `fa309a2dc89df90eb4ec790d58c844bd8f15be4e6ab7ff3c3a6b1d54418d7780`
- `git diff --check` 통과. 빌드에는 기존 make jobserver 경고가 있으나 성공적으로 링크됐다.

이번 검증 뒤 driver/filesystem suite를 재실행하거나 C++/network를 활성화하지 않았다. 후속 full set 작업에서는 먼저 남은 TC 기대값을 바로잡고, 저장장치 전제조건을 준비해야 한다.

## 새로 도달한 기존 테스트 실패

`tc_pthread_pthread_setcanceltype`는 `CONFIG_CANCELLATION_POINTS=y`에서 실행되면서도 `PTHREAD_CANCEL_DEFERRED` 설정에 `ENOSYS`를 기대한다. 실제 반환은 0이다. `lib/libc/pthread/pthread_setcanceltype.c`는 `task_setcanceltype()`을 호출하고, `os/kernel/task/task_setcanceltype.c`는 해당 요청에 deferred flag를 설정하고 OK를 반환한다. 이 경로는 condition timeout을 호출하지 않으며 이번 수정에도 포함하지 않았다.

따라서 이 FAIL은 정지 해소 후 드러난 기존 테스트 기대값과 구현의 불일치로 분류한다. 이번에는 실패를 숨기거나 집계에서 제외하지 않았으며, kernel suite 전체 성공으로 보고하지 않는다. 저장장치 부재에 따른 driver/filesystem baseline 실패도 별도로 남아 있다.

## 재현과 증거

Docker 빌드 이미지와 QEMU 이미지는 [0~1단계](QEMU_Build_Test_Stage01.md)와 같다. LM3S6965EVB / Cortex-M3 / ARMv7-M Flat / SRAM 16MiB에서 실행했다.

worktree 루트에서:

```sh
docker run --rm --pull=never --platform linux/arm64 \
  -v "$PWD:/work" -w /work/os tizenrt/tizenrt:2.0.1-arm64-local \
  bash -c 'set -e; make distclean; cd tools; ./configure.sh qemu/build_test; cd ..; make -j4'
cmp os/.config build/configs/qemu/build_test/defconfig
python3 tools/qemu-build-test/boot-smoke.py --root . \
  --output tmp/qemu-build-test-rwlock-fix/full-boot-new
python3 tools/qemu-build-test/run-testcases.py --root . --suite kernel_tc \
  --timeout 1200 --output tmp/qemu-build-test-rwlock-fix/full-kernel-new
```

로컬 원문 증거: `tmp/qemu-build-test-rwlock-fix/` (Git ignored).

- 각 실행 폴더의 `serial.log`, `result.json`
- `baseline.config`, `minimal.config`, `capacity3.config`, `capacity1.config`, `full.config`
- `capacity1-test.c`: watchdog 등록 실패 경로만 실행한 진단 소스
- `build-*.log`, red/green/full ELF 및 최종 BIN
- `manifest.json`: 최종 소스 diff, 구성, firmware hash와 실행 요약

C++, SmartFS 장치/마운트, 네트워크 확장은 후속 단계다. 이 검증은 QEMU runtime 증거이며 물리 보드 검증이나 CircleCI 실행 결과가 아니다.
