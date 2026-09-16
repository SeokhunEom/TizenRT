# TizenRT syscall 목록과 POSIX 표준 포함 여부

조사 기준: 2026-09-16, 로컬 `master` 체크아웃의 커밋 `29d2ed503c2915bd123ff020f5c76353aa19f34c`. 빌드·실기기 실행이나 POSIX 적합성 시험을 수행한 결과가 아니라, 등록표와 공식 표준을 대조한 소스 조사다.

## 범위와 요약

이 문서의 syscall은 [os/syscall/syscall.csv](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv)에 등록되고 [syscall_lookup.h](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall_lookup.h)에 나열된 인터페이스를 뜻한다. [Protected build 안내](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/docs/HowToSupportProtectedBuild.md#L305)도 이 CSV를 syscall API 목록으로 연결한다. libc·framework 전체 API 목록과는 범위가 다르다.

| 집계 항목 | 개수 |
| --- | ---: |
| 등록 이름 | 166 |
| 고유 일반 syscall 번호 | 165 |
| POSIX.1-2017과 POSIX.1-2024 모두 포함된 이름 | 128 |
| 2017에는 있으나 2024에서 제거된 이름 | 2 |
| 2017·2024 모두에 없는 이름 | 36 |

`ioctl`과 `fs_ioctl`은 같은 번호를 공유하고, `CONFIG_LIBC_IOCTL_VARIADIC`에 따라 lookup 항목이 하나만 선택된다. 따라서 이름은 166개, 일반 syscall 슬롯은 165개다. 아키텍처 전용 예약 번호는 별도다. 근거: [번호 정의](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/include/sys/syscall.h#L215), [lookup 선택](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall_lookup.h#L132), [개수 계산](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/include/sys/syscall.h#L343).

**O는 해당 API 이름이 표준에 정의되어 있다는 뜻이다. TizenRT 구현의 모든 인자·반환값·오류·실행 의미가 POSIX와 일치하거나, 모든 설정에서 호출 가능하다는 뜻은 아니다.** XSI 및 실시간 등 표준의 선택 기능도 O에 포함했다. ISO C에도 정의된 `exit`, `atexit`, `getenv`, `clock` 등 역시 POSIX 명세에 포함되므로 O다.

분류 근거: [POSIX.1-2017 함수 색인](https://pubs.opengroup.org/onlinepubs/9699919799/idx/functions.html), [POSIX.1-2024 함수 색인](https://pubs.opengroup.org/onlinepubs/9799919799/idx/functions.html), [Issue 8 제거 목록](https://pubs.opengroup.org/onlinepubs/9799919799/xrat/V4_xsh_chap01.html). X는 이 두 버전의 함수 색인에 그 이름의 인터페이스가 없다는 뜻이며, 항상 TizenRT만의 독자 기능이라는 뜻은 아니다.

## 두 표준에 모두 포함되는 128개

| 기능 | 개수 | syscall 이름 |
| --- | ---: | --- |
| 프로세스·종료·대기 | 8 | `_exit`, `exit`, `atexit`, `getpid`, `execv`, `wait`, `waitpid`, `waitid` |
| 스케줄링 | 6 | `sched_getparam`, `sched_getscheduler`, `sched_rr_get_interval`, `sched_setparam`, `sched_setscheduler`, `sched_yield` |
| 세마포어 | 8 | `sem_destroy`, `sem_post`, `sem_timedwait`, `sem_trywait`, `sem_wait`, `sem_open`, `sem_close`, `sem_unlink` |
| 스레드 생명주기·정리 | 7 | `pthread_create`, `pthread_detach`, `pthread_exit`, `pthread_join`, `pthread_cancel`, `pthread_cleanup_push`, `pthread_cleanup_pop` |
| 뮤텍스 | 6 | `pthread_mutex_init`, `pthread_mutex_destroy`, `pthread_mutex_lock`, `pthread_mutex_trylock`, `pthread_mutex_unlock`, `pthread_mutex_consistent` |
| 조건변수 | 4 | `pthread_cond_broadcast`, `pthread_cond_signal`, `pthread_cond_wait`, `pthread_cond_timedwait` |
| 스레드 스케줄링·TLS | 7 | `pthread_getschedparam`, `pthread_setschedparam`, `pthread_setschedprio`, `pthread_key_create`, `pthread_key_delete`, `pthread_getspecific`, `pthread_setspecific` |
| 시그널 | 10 | `kill`, `sigaction`, `sigpending`, `sigprocmask`, `sigqueue`, `sigsuspend`, `sigtimedwait`, `sigwaitinfo`, `pthread_kill`, `pthread_sigmask` |
| 시계·타이머 | 10 | `clock`, `clock_getres`, `clock_gettime`, `clock_settime`, `nanosleep`, `timer_create`, `timer_delete`, `timer_getoverrun`, `timer_gettime`, `timer_settime` |
| 파일 I/O·다중화 | 14 | `open`, `close`, `read`, `write`, `pread`, `pwrite`, `lseek`, `dup`, `dup2`, `fcntl`, `fsync`, `ftruncate`, `poll`, `select` |
| 파일·디렉터리 | 11 | `stat`, `fstat`, `mkdir`, `rmdir`, `rename`, `unlink`, `opendir`, `closedir`, `readdir`, `rewinddir`, `seekdir` |
| 파이프·메모리 매핑 | 3 | `pipe`, `mkfifo`, `mmap` |
| 비동기 I/O | 4 | `aio_read`, `aio_write`, `aio_fsync`, `aio_cancel` |
| 메시지 큐 | 10 | `mq_open`, `mq_close`, `mq_getattr`, `mq_setattr`, `mq_notify`, `mq_receive`, `mq_send`, `mq_timedreceive`, `mq_timedsend`, `mq_unlink` |
| 환경변수 | 4 | `getenv`, `putenv`, `setenv`, `unsetenv` |
| 소켓 | 16 | `socket`, `bind`, `listen`, `accept`, `connect`, `shutdown`, `getsockname`, `getpeername`, `getsockopt`, `setsockopt`, `recv`, `recvfrom`, `recvmsg`, `send`, `sendto`, `sendmsg` |

## 표준 버전에 따라 달라지는 항목

| API | POSIX.1-2017 | POSIX.1-2024 | 해석 |
| --- | --- | --- | --- |
| `gettimeofday` | O: OB·XSI | X: 제거됨 | 2017의 폐기 예정 XSI 인터페이스. 새 코드에서 시간 조회는 `clock_gettime` 검토. |
| `ioctl` | O: OB·XSR | X: 제거됨 | 2017 명세는 STREAMS 옵션용이며, 일반 장치 제어 동작까지 표준화하지 않는다. TizenRT의 ioctl 요청 코드가 이식 가능하다는 뜻이 아니다. |
| `vfork` | X | X | POSIX.1-2001/Issue 6에는 OB·XSI로 있었지만 POSIX.1-2008/Issue 7에서 제거됐다. 위 36개에 포함한다. |

근거: [2017 gettimeofday](https://pubs.opengroup.org/onlinepubs/9699919799/functions/gettimeofday.html), [2017 ioctl](https://pubs.opengroup.org/onlinepubs/9699919799/functions/ioctl.html), [Issue 6 vfork](https://pubs.opengroup.org/onlinepubs/009695399/functions/vfork.html), [Issue 7 변경 내역](https://pubs.opengroup.org/onlinepubs/9699919799/xrat/V4_xsh_chap01.html), [Issue 8 변경 내역](https://pubs.opengroup.org/onlinepubs/9799919799/xrat/V4_xsh_chap01.html). OB는 obsolescent, XSI는 X/Open System Interfaces, XSR은 XSI STREAMS 옵션이다.

`seekdir`는 두 버전 모두 XSI에 포함된다. `pthread_cleanup_push`·`pthread_cleanup_pop` 및 `pthread_mutex_consistent`도 표준 인터페이스다. cleanup 인터페이스는 표준상 매크로로 구현할 수도 있지만, TizenRT 등록표에는 syscall 이름으로 들어 있다. 근거: [seekdir](https://pubs.opengroup.org/onlinepubs/9799919799/functions/seekdir.html), [cleanup](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_cleanup_push.html), [mutex consistent](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_mutex_consistent.html).

## 두 표준에 없는 36개

| 기능 | 개수 | syscall 이름 |
| --- | ---: | --- |
| TizenRT 태스크·로더 | 7 | `task_create`, `task_delete`, `task_restart`, `task_testcancel`, `task_setcancelstate`, `task_setcanceltype`, `exec` |
| 스케줄러·CPU 확장 | 7 | `sched_lock`, `sched_lockcount`, `sched_unlock`, `sched_getaffinity`, `sched_setaffinity`, `sched_getcpu`, `sched_getcpucount` |
| pthread·세마포어 확장 | 4 | `pthread_tryjoin_np`, `pthread_getaffinity_np`, `pthread_setaffinity_np`, `sem_setprotocol` |
| 파일시스템·스트림 확장 | 7 | `mount`, `umount`, `statfs`, `fstatfs`, `fs_fdopen`, `fs_ioctl`, `sched_getstreams` |
| 내부 상태·보드·IRQ | 7 | `get_errno`, `set_errno`, `get_environ_ptr`, `pgalloc`, `up_assert`, `boardctl`, `fin_wait` |
| 기타 확장·과거 표준 | 4 | `clearenv`, `on_exit`, `prctl`, `vfork` |

`exec`는 TizenRT 바이너리 로더 인터페이스 이름이며 표준 `execv`와 구분한다. `fs_fdopen`은 표준 `fdopen`과 다르고, `statfs`·`fstatfs`는 표준 `statvfs`·`fstatvfs`와 다르다. `_np` 함수나 CPU affinity 관련 함수가 다른 OS에도 존재한다는 사실만으로 POSIX가 되지는 않는다. 근거: 위 공식 함수 색인과 아래 각 CSV 행.

## 등록과 실제 지원을 구분할 사항

- **빌드 조건:** CSV 세 번째 필드가 proxy/stub 생성 조건이다. 조건을 만족하지 않는 stub의 생성 경로는 `errno=ENOSYS`와 실패 반환을 출력한다. 번호가 있다는 사실만으로 활성화된 기능은 아니다. [CSV 설명](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/README.txt#L80), [stub 생성 코드](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/tools/mksyscall.c#L510).
- **`mmap`:** 표준 이름이고 CSV에 등록되어 있지만 이 커밋의 lookup 대상은 `NULL`이다. 정상적인 syscall dispatch가 연결됐다고 볼 수 없다. [lookup](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall_lookup.h#L159).
- **`aio_read`, `aio_write`, `aio_fsync`, `aio_cancel`:** lookup 세 번째 인자가 `STUB_aio_*`가 아니라 `SYS_aio_*` 번호로 적혀 있다. `g_stublookup`은 이 인자를 주소로 저장하므로, 등록된 네 항목을 정상 호출 가능한 syscall로 단정할 수 없다. [lookup](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall_lookup.h#L144), [테이블 생성](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall_stublookup.c#L376). 실행 재현은 하지 않았다.
- **동일 이름의 실행 의미:** TizenRT `getpid` 구현은 현재 TCB의 task ID를 반환하고, `pthread_self`도 이를 사용한다. 이 목록의 O 표기를 일반적인 프로세스 모델까지 동일하다는 의미로 해석하면 안 된다. [getpid 구현](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/kernel/task/task_getpid.c#L89), [pthread_self](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/include/pthread.h#L441).
- **목록 밖의 POSIX API:** `sem_init`는 libc에 구현되어 있고 `pthread_self`는 매크로다. `pthread_setcancelstate`는 libc에서 비표준 내부 syscall `task_setcancelstate`를 호출한다. 그러므로 syscall CSV에 없는 API를 곧바로 미지원 API라고 판단하면 안 된다. [sem_init](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/lib/libc/semaphore/sem_init.c#L104), [pthread_self](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/include/pthread.h#L441), [pthread_setcancelstate](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/lib/libc/pthread/pthread_setcancelstate.c#L103).

## 전체 166개 상세표

번호는 `CONFIG_SYS_RESERVED`를 빼고 표시한 오프셋이다. 실제 번호는 `CONFIG_SYS_RESERVED + 오프셋`이다. 조건 `—`는 CSV의 조건 필드가 비어 있다는 뜻이며, 전체 syscall 계층의 `CONFIG_LIB_SYSCALL` 등 전제까지 없다는 뜻은 아니다. C01~C35의 원문은 다음 절에 있다. API 링크는 해당 CSV 행으로 연결한다.

| 등록 API | 번호 오프셋 | POSIX 2017 | POSIX 2024 | CSV 조건 |
| --- | ---: | :---: | :---: | --- |
| [_exit](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L1) | 0 | O | O | — |
| [aio_cancel](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L2) | 71 | O | O | C01 |
| [aio_fsync](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L3) | 70 | O | O | C01 |
| [aio_read](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L4) | 68 | O | O | C01 |
| [aio_write](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L5) | 69 | O | O | C01 |
| [accept](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L6) | 147 | O | O | C02 |
| [atexit](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L7) | 36 | O | O | C03 |
| [bind](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L8) | 148 | O | O | C02 |
| [boardctl](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L9) | 74 | X | X | C04 |
| [clearenv](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L10) | 141 | X | X | C05 |
| [clock](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L11) | 52 | O | O | — |
| [clock_getres](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L12) | 53 | O | O | — |
| [clock_gettime](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L13) | 54 | O | O | — |
| [clock_settime](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L14) | 55 | O | O | — |
| [close](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L15) | 62 | O | O | C06 |
| [closedir](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L16) | 75 | O | O | C07 |
| [connect](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L17) | 149 | O | O | C02 |
| [dup](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L18) | 76 | O | O | C07 |
| [dup2](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L19) | 77 | O | O | C07 |
| [exec](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L20) | 41 | X | X | C08 |
| [execv](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L21) | 42 | O | O | C09 |
| [exit](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L22) | 1 | O | O | — |
| [fcntl](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L23) | 78 | O | O | C07 |
| [fin_wait](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L24) | 164 | X | X | — |
| [fs_fdopen](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L25) | 92 | X | X | C10 |
| [fs_ioctl](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L26) | 63 | X | X | C11 |
| [fstat](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L27) | 79 | O | O | C07 |
| [fstatfs](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L28) | 80 | X | X | C07 |
| [fsync](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L29) | 94 | O | O | C12 |
| [ftruncate](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L30) | 101 | O | O | C12 |
| [get_errno](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L31) | 2 | X | X | C13 |
| [getenv](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L32) | 142 | O | O | C05 |
| [get_environ_ptr](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L33) | 146 | X | X | C05 |
| [getpeername](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L34) | 150 | O | O | C02 |
| [getpid](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L35) | 3 | O | O | — |
| [getsockname](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L36) | 151 | O | O | C02 |
| [getsockopt](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L37) | 152 | O | O | C02 |
| [gettimeofday](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L38) | 56 | O (OB·XSI) | X | — |
| [ioctl](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L39) | 63 | O (OB·XSR) | X | C14 |
| [kill](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L40) | 43 | O | O | C15 |
| [listen](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L41) | 153 | O | O | C02 |
| [lseek](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L42) | 81 | O | O | C07 |
| [mkdir](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L43) | 95 | O | O | C12 |
| [mkfifo](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L44) | 82 | O | O | C16 |
| [mmap](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L45) | 83 | O | O | C07 |
| [mount](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L46) | 96 | X | X | C17 |
| [mq_close](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L47) | 131 | O | O | C18 |
| [mq_getattr](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L48) | 132 | O | O | C18 |
| [mq_notify](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L49) | 133 | O | O | C19 |
| [mq_open](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L50) | 134 | O | O | C18 |
| [mq_receive](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L51) | 135 | O | O | C18 |
| [mq_send](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L52) | 136 | O | O | C18 |
| [mq_setattr](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L53) | 137 | O | O | C18 |
| [mq_timedreceive](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L54) | 138 | O | O | C18 |
| [mq_timedsend](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L55) | 139 | O | O | C18 |
| [mq_unlink](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L56) | 140 | O | O | C18 |
| [on_exit](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L57) | 37 | X | X | C20 |
| [nanosleep](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L58) | 51 | O | O | C15 |
| [open](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L59) | 84 | O | O | C07 |
| [opendir](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L60) | 85 | O | O | C07 |
| [pgalloc](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L61) | 28 | X | X | C21 |
| [pipe](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L62) | 86 | O | O | C16 |
| [poll](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L63) | 72 | O | O | C22 |
| [prctl](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L64) | 163 | X | X | C23 |
| [pread](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L65) | 66 | O | O | C06 |
| [pwrite](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L66) | 67 | O | O | C06 |
| [pthread_cancel](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L67) | 102 | O | O | C24 |
| [pthread_cleanup_pop](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L68) | 129 | O | O | C25 |
| [pthread_cleanup_push](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L69) | 130 | O | O | C25 |
| [pthread_cond_broadcast](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L70) | 103 | O | O | C24 |
| [pthread_cond_signal](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L71) | 104 | O | O | C24 |
| [pthread_cond_timedwait](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L72) | 126 | O | O | C26 |
| [pthread_cond_wait](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L73) | 105 | O | O | C24 |
| [pthread_create](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L74) | 106 | O | O | C24 |
| [pthread_detach](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L75) | 107 | O | O | C24 |
| [pthread_exit](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L76) | 108 | O | O | C24 |
| [pthread_getschedparam](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L77) | 109 | O | O | C24 |
| [pthread_getspecific](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L78) | 110 | O | O | C24 |
| [pthread_join](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L79) | 111 | O | O | C24 |
| [pthread_tryjoin_np](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L80) | 112 | X | X | C24 |
| [pthread_key_create](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L81) | 113 | O | O | C24 |
| [pthread_key_delete](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L82) | 114 | O | O | C24 |
| [pthread_kill](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L83) | 127 | O | O | C26 |
| [pthread_mutex_destroy](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L84) | 115 | O | O | C24 |
| [pthread_mutex_init](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L85) | 116 | O | O | C24 |
| [pthread_mutex_lock](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L86) | 117 | O | O | C24 |
| [pthread_mutex_trylock](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L87) | 118 | O | O | C24 |
| [pthread_mutex_unlock](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L88) | 119 | O | O | C24 |
| [pthread_mutex_consistent](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L89) | 120 | O | O | C27 |
| [pthread_setaffinity_np](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L90) | 121 | X | X | C24 |
| [pthread_getaffinity_np](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L91) | 122 | X | X | C24 |
| [pthread_setschedparam](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L92) | 123 | O | O | C24 |
| [pthread_setschedprio](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L93) | 124 | O | O | C24 |
| [pthread_setspecific](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L94) | 125 | O | O | C24 |
| [pthread_sigmask](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L95) | 128 | O | O | C26 |
| [putenv](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L96) | 143 | O | O | C05 |
| [read](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L97) | 64 | O | O | C06 |
| [readdir](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L98) | 87 | O | O | C07 |
| [recv](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L99) | 154 | O | O | C02 |
| [recvfrom](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L100) | 155 | O | O | C02 |
| [recvmsg](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L101) | 156 | O | O | C02 |
| [rename](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L102) | 97 | O | O | C12 |
| [rewinddir](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L103) | 88 | O | O | C07 |
| [rmdir](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L104) | 98 | O | O | C12 |
| [sched_getaffinity](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L105) | 14 | X | X | — |
| [sched_setaffinity](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L106) | 15 | X | X | — |
| [sched_getcpu](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L107) | 16 | X | X | — |
| [sched_getcpucount](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L108) | 17 | X | X | — |
| [sched_getparam](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L109) | 4 | O | O | — |
| [sched_getscheduler](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L110) | 5 | O | O | — |
| [sched_getstreams](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L111) | 93 | X | X | C10 |
| [sched_lock](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L112) | 6 | X | X | — |
| [sched_lockcount](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L113) | 7 | X | X | — |
| [sched_rr_get_interval](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L114) | 8 | O | O | — |
| [sched_setparam](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L115) | 9 | O | O | — |
| [sched_setscheduler](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L116) | 10 | O | O | — |
| [sched_unlock](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L117) | 11 | X | X | — |
| [sched_yield](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L118) | 12 | O | O | — |
| [seekdir](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L119) | 89 | O (XSI) | O (XSI) | C07 |
| [select](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L120) | 73 | O | O | C22 |
| [sem_close](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L121) | 25 | O | O | C28 |
| [sem_destroy](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L122) | 18 | O | O | — |
| [sem_open](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L123) | 24 | O | O | C28 |
| [sem_post](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L124) | 19 | O | O | — |
| [sem_setprotocol](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L125) | 23 | X | X | C29 |
| [sem_timedwait](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L126) | 20 | O | O | — |
| [sem_trywait](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L127) | 21 | O | O | — |
| [sem_unlink](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L128) | 26 | O | O | C28 |
| [sem_wait](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L129) | 22 | O | O | — |
| [send](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L130) | 157 | O | O | C02 |
| [sendmsg](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L131) | 158 | O | O | C02 |
| [sendto](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L132) | 159 | O | O | C02 |
| [set_errno](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L133) | 13 | X | X | C13 |
| [setenv](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L134) | 144 | O | O | C05 |
| [setsockopt](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L135) | 160 | O | O | C02 |
| [shutdown](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L136) | 161 | O | O | C02 |
| [sigaction](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L137) | 44 | O | O | C15 |
| [sigpending](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L138) | 45 | O | O | C15 |
| [sigprocmask](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L139) | 46 | O | O | C15 |
| [sigqueue](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L140) | 47 | O | O | C15 |
| [sigsuspend](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L141) | 48 | O | O | C15 |
| [sigtimedwait](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L142) | 49 | O | O | C15 |
| [sigwaitinfo](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L143) | 50 | O | O | C15 |
| [socket](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L144) | 162 | O | O | C02 |
| [stat](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L145) | 90 | O | O | C07 |
| [statfs](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L146) | 91 | X | X | C07 |
| [task_create](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L147) | 27 | X | X | C30 |
| [task_delete](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L148) | 29 | X | X | — |
| [task_restart](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L149) | 30 | X | X | — |
| [task_setcancelstate](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L150) | 33 | X | X | — |
| [task_setcanceltype](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L151) | 32 | X | X | C31 |
| [task_testcancel](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L152) | 31 | X | X | C31 |
| [timer_create](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L153) | 57 | O | O | C32 |
| [timer_delete](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L154) | 58 | O | O | C32 |
| [timer_getoverrun](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L155) | 59 | O | O | C32 |
| [timer_gettime](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L156) | 60 | O | O | C32 |
| [timer_settime](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L157) | 61 | O | O | C32 |
| [umount](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L158) | 99 | X | X | C12 |
| [unlink](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L159) | 100 | O | O | C12 |
| [unsetenv](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L160) | 145 | O | O | C05 |
| [up_assert](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L161) | 34 | X | X | — |
| [vfork](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L162) | 35 | X (과거 표준) | X | C33 |
| [wait](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L163) | 39 | O | O | C34 |
| [waitid](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L164) | 40 | O | O | C34 |
| [waitpid](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L165) | 38 | O | O | C35 |
| [write](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/syscall/syscall.csv#L166) | 65 | O | O | C06 |

## CSV의 활성 조건 원문

조건은 설정을 변경하라는 권장이 아니라 이 소스 버전의 등록 정보다. `defined`와 `!defined`의 방향을 그대로 보존했다.

| 조건 | C 전처리식 |
| --- | --- |
| C01 | `defined(CONFIG_FS_AIO)` |
| C02 | `CONFIG_NSOCKET_DESCRIPTORS > 0 && defined(CONFIG_NET)` |
| C03 | `defined(CONFIG_SCHED_ATEXIT)` |
| C04 | `defined(CONFIG_LIB_BOARDCTL)` |
| C05 | `!defined(CONFIG_DISABLE_ENVIRON)` |
| C06 | `CONFIG_NSOCKET_DESCRIPTORS > 0 \|\| CONFIG_NFILE_DESCRIPTORS > 0` |
| C07 | `CONFIG_NFILE_DESCRIPTORS > 0` |
| C08 | `defined(CONFIG_BINFMT_ENABLE) && !defined(CONFIG_BUILD_KERNEL)` |
| C09 | `defined(CONFIG_LIBC_EXECFUNCS)` |
| C10 | `CONFIG_NFILE_DESCRIPTORS > 0 && CONFIG_NFILE_STREAMS > 0` |
| C11 | `defined(CONFIG_LIBC_IOCTL_VARIADIC) && (CONFIG_NSOCKET_DESCRIPTORS > 0 \|\| CONFIG_NFILE_DESCRIPTORS > 0)` |
| C12 | `CONFIG_NFILE_DESCRIPTORS > 0 && !defined(CONFIG_DISABLE_MOUNTPOINT)` |
| C13 | `!defined(__DIRECT_ERRNO_ACCESS)` |
| C14 | `!defined(CONFIG_LIBC_IOCTL_VARIADIC) && (CONFIG_NSOCKET_DESCRIPTORS > 0 \|\| CONFIG_NFILE_DESCRIPTORS > 0)` |
| C15 | `!defined(CONFIG_DISABLE_SIGNALS)` |
| C16 | `defined(CONFIG_PIPES)` |
| C17 | `CONFIG_NFILE_DESCRIPTORS > 0 && !defined(CONFIG_DISABLE_MOUNTPOINT) && defined(CONFIG_FS_READABLE)` |
| C18 | `!defined(CONFIG_DISABLE_MQUEUE)` |
| C19 | `!defined(CONFIG_DISABLE_SIGNALS) && !defined(CONFIG_DISABLE_MQUEUE)` |
| C20 | `defined(CONFIG_SCHED_ONEXIT)` |
| C21 | `defined(CONFIG_BUILD_KERNEL)` |
| C22 | `!defined(CONFIG_DISABLE_POLL) && (CONFIG_NSOCKET_DESCRIPTORS > 0 \|\| CONFIG_NFILE_DESCRIPTORS > 0)` |
| C23 | `CONFIG_TASK_NAME_SIZE > 0` |
| C24 | `!defined(CONFIG_DISABLE_PTHREAD)` |
| C25 | `defined(CONFIG_PTHREAD_CLEANUP)` |
| C26 | `!defined(CONFIG_DISABLE_SIGNALS) && !defined(CONFIG_DISABLE_PTHREAD)` |
| C27 | `!defined(CONFIG_DISABLE_PTHREAD) && !defined(CONFIG_PTHREAD_MUTEX_UNSAFE)` |
| C28 | `defined(CONFIG_FS_NAMED_SEMAPHORES)` |
| C29 | `defined(CONFIG_PRIORITY_INHERITANCE)` |
| C30 | `!defined(CONFIG_BUILD_KERNEL)` |
| C31 | `defined(CONFIG_CANCELLATION_POINTS)` |
| C32 | `!defined(CONFIG_DISABLE_POSIX_TIMERS)` |
| C33 | `defined(CONFIG_ARCH_HAVE_VFORK) && defined(CONFIG_SCHED_WAITPID)` |
| C34 | `defined(CONFIG_SCHED_WAITPID) && defined(CONFIG_SCHED_HAVE_PARENT)` |
| C35 | `defined(CONFIG_SCHED_WAITPID)` |

## 아키텍처 전용 예약 호출

[ARMv7-A syscall 헤더](https://github.com/Samsung/TizenRT/blob/29d2ed503c2915bd123ff020f5c76353aa19f34c/os/arch/arm/include/armv7-a/syscall.h#L86)의 protected-build 예약 호출은 아래와 같다. 이 8개는 일반 syscall 165개 집계에 포함하지 않으며 POSIX API가 아니다. 다른 아키텍처·빌드 모드의 예약 개수는 해당 헤더와 설정을 확인해야 한다.

| 예약 번호 | 이름 |
| ---: | --- |
| 0 | `SYS_save_context` |
| 1 | `SYS_restore_context` |
| 2 | `SYS_switch_context` |
| 3 | `SYS_syscall_return` |
| 4 | `SYS_task_start` |
| 5 | `SYS_pthread_start` |
| 6 | `SYS_signal_handler` |
| 7 | `SYS_signal_handler_return` |

## 검증 범위

- CSV 이름의 앞뒤 공백을 정규화한 후 166개 고유 이름을 추출하고 lookup의 이름 집합과 일치함을 확인했다.
- 분류 집합은 128 + 2 + 36 = 166이며 중복·누락이 없음을 확인했다.
- `ioctl`/`fs_ioctl`의 번호 공유를 반영해 165개 슬롯으로 계산했다.
- 166개 이름 전체를 두 버전의 Open Group 공식 함수 색인 원문과 독립 대조했다. 2017은 130개, 2024는 128개가 일치하며, 이 문서의 분류와 양방향 차집합이 모두 비어 있음을 확인했다.
- 모든 번호 오프셋을 `sys/syscall.h`의 매크로 정의와 대조해 일치함을 확인했다.
- 모든 구현의 POSIX 동작 적합성이나 특정 보드에서의 실행 성공까지 검증한 것은 아니다.

## 별도 syscall 번호가 없는 주요 POSIX API

이 절은 후속 질문을 위한 주요 API 목록이다. **syscall.csv에 이름이 없는 것**과 **커널에 진입하지 않는 것**을 구분한다. 아래 API는 libc 함수나 매크로로 제공되며, 내부에서 다른 syscall을 호출할 수 있다. 전체 POSIX API 제공 개수를 집계한 표는 아니다. 목록 밖의 API를 미지원으로 해석하면 안 된다.

아래 이름은 POSIX.1-2017과 POSIX.1-2024 함수 색인에 모두 있고, 이 소스 트리의 구현 또는 매크로를 확인했으며, 166개 syscall 이름 집합과 겹치지 않는다. 수학·와이드문자 등의 전체 목록은 생략했다. 기능 활성화 및 실제 실행 의미는 빌드 설정과 구현에 따라 달라진다.

| 기능 | 별도 syscall 번호 없는 POSIX API |
| --- | --- |
| 세마포어 | [sem_init](../lib/libc/semaphore/sem_init.c#L103), [sem_getvalue](../lib/libc/semaphore/sem_getvalue.c#L113) |
| 조건변수·1회 초기화 | [pthread_cond_init](../lib/libc/pthread/pthread_condinit.c#L84), [pthread_cond_destroy](../lib/libc/pthread/pthread_conddestroy.c#L84), [pthread_once](../lib/libc/pthread/pthread_once.c#L116) |
| 읽기·쓰기 잠금 | [pthread_rwlock_init](../lib/libc/pthread/pthread_rwlock.c#L71), [pthread_rwlock_destroy](../lib/libc/pthread/pthread_rwlock.c#L101), [pthread_rwlock_rdlock](../lib/libc/pthread/pthread_rwlock_rdlock.c#L159), [pthread_rwlock_wrlock](../lib/libc/pthread/pthread_rwlock_wrlock.c#L167), [pthread_rwlock_tryrdlock](../lib/libc/pthread/pthread_rwlock_rdlock.c#L116), [pthread_rwlock_trywrlock](../lib/libc/pthread/pthread_rwlock_wrlock.c#L101), [pthread_rwlock_timedrdlock](../lib/libc/pthread/pthread_rwlock_rdlock.c#L130), [pthread_rwlock_timedwrlock](../lib/libc/pthread/pthread_rwlock_wrlock.c#L119), [pthread_rwlock_unlock](../lib/libc/pthread/pthread_rwlock.c#L113) |
| 배리어 | [pthread_barrier_init](../lib/libc/pthread/pthread_barrierinit.c#L124), [pthread_barrier_destroy](../lib/libc/pthread/pthread_barrierdestroy.c#L114), [pthread_barrier_wait](../lib/libc/pthread/pthread_barrierwait.c#L132), [pthread_barrierattr_init](../lib/libc/pthread/pthread_barrierattrinit.c#L106), [pthread_barrierattr_destroy](../lib/libc/pthread/pthread_barrierattrdestroy.c#L107), [pthread_barrierattr_getpshared](../lib/libc/pthread/pthread_barrierattrgetpshared.c#L106), [pthread_barrierattr_setpshared](../lib/libc/pthread/pthread_barrierattrsetpshared.c#L116) |
| 스레드 속성 | [pthread_attr_init](../lib/libc/pthread/pthread_attrinit.c#L120), [pthread_attr_destroy](../lib/libc/pthread/pthread_attrdestroy.c#L105), [pthread_attr_getschedpolicy](../lib/libc/pthread/pthread_attrgetschedpolicy.c#L104), [pthread_attr_setschedpolicy](../lib/libc/pthread/pthread_attrsetschedpolicy.c#L107), [pthread_attr_getschedparam](../lib/libc/pthread/pthread_attrgetschedparam.c#L106), [pthread_attr_setschedparam](../lib/libc/pthread/pthread_attrsetschedparam.c#L106), [pthread_attr_getinheritsched](../lib/libc/pthread/pthread_attrgetinheritsched.c#L108), [pthread_attr_setinheritsched](../lib/libc/pthread/pthread_attrsetinheritsched.c#L109), [pthread_attr_getstacksize](../lib/libc/pthread/pthread_attrgetstacksize.c#L103), [pthread_attr_setstacksize](../lib/libc/pthread/pthread_attrsetstacksize.c#L105) |
| 뮤텍스 속성 | [pthread_mutexattr_init](../lib/libc/pthread/pthread_mutexattrinit.c#L104), [pthread_mutexattr_destroy](../lib/libc/pthread/pthread_mutexattrdestroy.c#L104), [pthread_mutexattr_getpshared](../lib/libc/pthread/pthread_mutexattrgetpshared.c#L105), [pthread_mutexattr_setpshared](../lib/libc/pthread/pthread_mutexattrsetpshared.c#L105), [pthread_mutexattr_gettype](../lib/libc/pthread/pthread_mutexattrgettype.c#L105), [pthread_mutexattr_settype](../lib/libc/pthread/pthread_mutexattrsettype.c#L105), [pthread_mutexattr_getprotocol](../lib/libc/pthread/pthread_mutexattr_getprotocol.c#L85), [pthread_mutexattr_setprotocol](../lib/libc/pthread/pthread_mutexattr_setprotocol.c#L84), [pthread_mutexattr_getrobust](../lib/libc/pthread/pthread_mutexattr_getrobust.c#L84), [pthread_mutexattr_setrobust](../lib/libc/pthread/pthread_mutexattr_setrobust.c#L84) |
| 조건변수 속성 | [pthread_condattr_init](../lib/libc/pthread/pthread_condattrinit.c#L84), [pthread_condattr_destroy](../lib/libc/pthread/pthread_condattrdestroy.c#L84) |
| 스레드 ID·취소 | [pthread_self](../os/include/pthread.h#L441), [pthread_equal](../os/include/pthread.h#L452), [pthread_setcancelstate](../lib/libc/pthread/pthread_setcancelstate.c#L95), [pthread_setcanceltype](../lib/libc/pthread/pthread_setcanceltype.c#L96), [pthread_testcancel](../lib/libc/pthread/pthread_testcancel.c#L74) |
| 스케줄링 범위 조회 | [sched_get_priority_max](../lib/libc/sched/sched_getprioritymax.c#L107), [sched_get_priority_min](../lib/libc/sched/sched_getprioritymin.c#L107) |
| 시그널 | [sigemptyset](../lib/libc/signal/sig_emptyset.c#L100), [sigfillset](../lib/libc/signal/sig_fillset.c#L100), [sigaddset](../lib/libc/signal/sig_addset.c#L101), [sigdelset](../lib/libc/signal/sig_delset.c#L101), [sigismember](../lib/libc/signal/sig_ismember.c#L103), [signal](../lib/libc/signal/signal.c#L89), [sigwait](../lib/libc/signal/sigwait.c#L106), [raise](../lib/libc/signal/sig_raise.c#L94), [psignal](../lib/libc/signal/sig_psignal.c#L91), [psiginfo](../lib/libc/signal/sig_psignal.c#L126) |
| 파일·디렉터리·실행 | [creat](../os/include/fcntl.h#L163), [fdatasync](../os/include/unistd.h#L118), [chdir](../lib/libc/unistd/lib_chdir.c#L133), [getcwd](../lib/libc/unistd/lib_getcwd.c#L113), [execl](../lib/libc/unistd/lib_execl.c#L146), [readv](../lib/libc/uio/lib_readv.c#L94), [writev](../lib/libc/uio/lib_writev.c#L100), [telldir](../lib/libc/dirent/lib_telldir.c#L94), [readdir_r](../lib/libc/dirent/lib_readdirr.c#L102) |
| 대기·시간 | [sleep](../lib/libc/unistd/lib_sleep.c#L146), [time](../lib/libc/time/lib_time.c#L102), [difftime](../lib/libc/time/lib_difftime.c#L78), [mktime](../lib/libc/time/lib_localtime.c#L2171), [gmtime](../lib/libc/time/lib_gmtime.c#L105), [gmtime_r](../lib/libc/time/lib_gmtimer.c#L315), [localtime](../lib/libc/time/lib_localtime.c#L2124), [localtime_r](../lib/libc/time/lib_localtime.c#L2132), [asctime](../lib/libc/time/lib_asctime.c#L82), [asctime_r](../lib/libc/time/lib_asctimer.c#L100), [ctime](../lib/libc/time/lib_ctime.c#L82), [ctime_r](../lib/libc/time/lib_ctimer.c#L87), [strftime](../lib/libc/time/lib_strftime.c#L174), [strptime](../lib/libc/time/lib_strptime.c#L132), [tzset](../lib/libc/time/lib_localtime.c#L1403) |
| 비동기 I/O 보조 | [aio_error](../lib/libc/aio/aio_error.c#L119), [aio_return](../lib/libc/aio/aio_return.c#L123), [aio_suspend](../lib/libc/aio/aio_suspend.c#L137), [lio_listio](../lib/libc/aio/lio_listio.c#L534) |
| 단말 설정 | [isatty](../lib/libc/termios/lib_isatty.c#L85), [tcgetattr](../lib/libc/termios/lib_tcgetattr.c#L107), [tcsetattr](../lib/libc/termios/lib_tcsetattr.c#L132), [tcflush](../lib/libc/termios/lib_tcflush.c#L102), [cfgetispeed](../os/include/termios.h#L289), [cfgetospeed](../os/include/termios.h#L290), [cfsetispeed](../os/include/termios.h#L302), [cfsetospeed](../os/include/termios.h#L303) |
| 네트워크 주소 | [getaddrinfo](../lib/libc/netdb/lib_getaddrinfo.c#L63), [freeaddrinfo](../lib/libc/netdb/lib_freeaddrinfo.c#L52), [getnameinfo](../lib/libc/netdb/lib_getnameinfo.c#L60), [inet_addr](../lib/libc/net/lib_inetaddr.c#L83), [inet_ntoa](../lib/libc/net/lib_inetntoa.c#L82), [inet_pton](../lib/libc/net/lib_inetpton.c#L359), [inet_ntop](../lib/libc/net/lib_inetntop.c#L251), [htons](../lib/libc/net/lib_htons.c#L66), [htonl](../lib/libc/net/lib_htonl.c#L66), [ntohs](../lib/libc/net/lib_htons.c#L71), [ntohl](../lib/libc/net/lib_htonl.c#L75) |
| 표준 입출력 | [fopen](../lib/libc/stdio/lib_fopen.c#L277), [fdopen](../lib/libc/stdio/lib_fopen.c#L258), [freopen](../lib/libc/stdio/lib_freopen.c#L101), [fclose](../lib/libc/stdio/lib_fclose.c#L87), [fread](../lib/libc/stdio/lib_fread.c#L100), [fwrite](../lib/libc/stdio/lib_fwrite.c#L100), [fseek](../lib/libc/stdio/lib_fseek.c#L125), [ftell](../lib/libc/stdio/lib_ftell.c#L153), [fgetpos](../lib/libc/stdio/lib_fgetpos.c#L120), [fsetpos](../lib/libc/stdio/lib_fsetpos.c#L122), [fflush](../lib/libc/stdio/lib_fflush.c#L115), [feof](../lib/libc/stdio/lib_feof.c#L83), [ferror](../lib/libc/stdio/lib_ferror.c#L82), [clearerr](../lib/libc/stdio/lib_clearerr.c#L81), [fileno](../lib/libc/stdio/lib_fileno.c#L70), [fgetc](../lib/libc/stdio/lib_fgetc.c#L104), [fgets](../lib/libc/stdio/lib_fgets.c#L87), [fputc](../lib/libc/stdio/lib_fputc.c#L104), [fputs](../lib/libc/stdio/lib_fputs.c#L116), [getc](../os/include/stdio.h#L145), [getchar](../os/include/stdio.h#L152), [putc](../os/include/stdio.h#L131), [putchar](../os/include/stdio.h#L138), [puts](../lib/libc/stdio/lib_puts.c#L108), [ungetc](../lib/libc/stdio/lib_ungetc.c#L108), [rewind](../os/include/stdio.h#L157), [setbuf](../lib/libc/stdio/lib_setbuf.c#L92), [setvbuf](../lib/libc/stdio/lib_setvbuf.c#L110), [printf](../lib/libc/stdio/lib_printf.c#L110), [fprintf](../lib/libc/stdio/lib_fprintf.c#L99), [snprintf](../lib/libc/stdio/lib_snprintf.c#L100), [sprintf](../lib/libc/stdio/lib_sprintf.c#L96), [vprintf](../lib/libc/stdio/lib_vprintf.c#L103), [vfprintf](../lib/libc/stdio/lib_vfprintf.c#L96), [vsnprintf](../lib/libc/stdio/lib_vsnprintf.c#L100), [vsprintf](../lib/libc/stdio/lib_vsprintf.c#L99), [dprintf](../lib/libc/stdio/lib_dprintf.c#L67), [vdprintf](../lib/libc/stdio/lib_vdprintf.c#L69), [sscanf](../lib/libc/stdio/lib_sscanf.c#L325), [vsscanf](../lib/libc/stdio/lib_sscanf.c#L344), [perror](../lib/libc/stdio/lib_perror.c#L106), [remove](../lib/libc/stdio/lib_remove.c#L81), [tmpnam](../lib/libc/stdio/lib_tmpnam.c#L100) |
| 메모리 할당 | [malloc](../os/mm/umm_heap/umm_malloc.c#L179), [calloc](../os/mm/umm_heap/umm_calloc.c#L150), [realloc](../os/mm/umm_heap/umm_realloc.c#L130), [free](../os/mm/umm_heap/umm_free.c#L82) |
| 문자열·메모리 처리 | [strlen](../lib/libc/string/lib_strlen.c#L66), [strnlen](../lib/libc/string/lib_strnlen.c#L73), [strcpy](../lib/libc/string/lib_strcpy.c#L78), [strncpy](../lib/libc/string/lib_strncpy.c#L70), [stpcpy](../lib/libc/string/lib_stpcpy.c#L79), [stpncpy](../lib/libc/string/lib_stpncpy.c#L89), [strcat](../lib/libc/string/lib_strcat.c#L66), [strncat](../lib/libc/string/lib_strncat.c#L66), [strcmp](../lib/libc/string/lib_strcmp.c#L66), [strncmp](../lib/libc/string/lib_strncmp.c#L70), [strcasecmp](../lib/libc/string/lib_strcasecmp.c#L67), [strncasecmp](../lib/libc/string/lib_strncasecmp.c#L72), [strchr](../lib/libc/string/lib_strchr.c#L80), [strrchr](../lib/libc/string/lib_strrchr.c#L69), [strstr](../lib/libc/string/lib_strstr.c#L65), [strspn](../lib/libc/string/lib_strspn.c#L78), [strcspn](../lib/libc/string/lib_strcspn.c#L78), [strpbrk](../lib/libc/string/lib_strpbrk.c#L65), [strtok](../lib/libc/string/lib_strtok.c#L101), [strtok_r](../lib/libc/string/lib_strtokr.c#L110), [strdup](../lib/libc/string/lib_strdup.c#L68), [strndup](../lib/libc/string/lib_strndup.c#L84), [strerror](../lib/libc/string/lib_strerror.c#L363), [strerror_r](../lib/libc/string/lib_strerrorr.c#L71), [strsignal](../lib/libc/string/lib_strsignal.c#L117), [strcoll](../lib/libc/string/lib_strcoll.c#L72), [strxfrm](../lib/libc/string/lib_strxfrm.c#L74), [memcpy](../lib/libc/string/lib_memcpy.c#L74), [memmove](../lib/libc/string/lib_memmove.c#L70), [memset](../lib/libc/string/lib_memset.c#L83), [memcmp](../lib/libc/string/lib_memcmp.c#L70), [memchr](../lib/libc/string/lib_memchr.c#L79), [memccpy](../lib/libc/string/lib_memccpy.c#L85) |
| 변환·탐색·정렬 | [atoi](../os/include/stdlib.h#L390), [atol](../os/include/stdlib.h#L398), [atoll](../os/include/stdlib.h#L407), [atof](../os/include/stdlib.h#L416), [strtol](../lib/libc/stdlib/lib_strtol.c#L91), [strtoul](../lib/libc/stdlib/lib_strtoul.c#L89), [strtoll](../lib/libc/stdlib/lib_strtoll.c#L93), [strtoull](../lib/libc/stdlib/lib_strtoull.c#L92), [strtof](../lib/libc/stdlib/lib_strtof.c#L109), [strtod](../lib/libc/stdlib/lib_strtod.c#L107), [strtold](../lib/libc/stdlib/lib_strtold.c#L107), [qsort](../lib/libc/stdlib/lib_qsort.c#L139), [bsearch](../lib/libc/stdlib/lib_bsearch.c#L123), [abs](../lib/libc/stdlib/lib_abs.c#L64), [labs](../lib/libc/stdlib/lib_labs.c#L64), [llabs](../lib/libc/stdlib/lib_llabs.c#L66), [div](../lib/libc/stdlib/lib_div.c#L88), [ldiv](../lib/libc/stdlib/lib_ldiv.c#L93), [lldiv](../lib/libc/stdlib/lib_lldiv.c#L93), [rand](../lib/libc/stdlib/lib_rand.c#L285), [srand](../lib/libc/stdlib/lib_rand.c#L272), [random](../lib/libc/stdlib/lib_rand.c#L299), [srandom](../os/include/stdlib.h#L195) |
| 문자 분류 | [isalnum](../os/include/ctype.h#L260), [isalpha](../os/include/ctype.h#L209), [isblank](../os/include/ctype.h#L226), [iscntrl](../os/include/ctype.h#L158), [isdigit](../os/include/ctype.h#L243), [isgraph](../os/include/ctype.h#L141), [islower](../os/include/ctype.h#L175), [isprint](../os/include/ctype.h#L124), [ispunct](../os/include/ctype.h#L278), [isspace](../os/include/ctype.h#L87), [isupper](../os/include/ctype.h#L192), [isxdigit](../os/include/ctype.h#L295), [tolower](../os/include/ctype.h#L333), [toupper](../os/include/ctype.h#L315) |
| 수학 예시 | [sin](../lib/libc/math/lib_sin.c#L77), [sinf](../lib/libc/math/lib_sinf.c#L69), [sinl](../lib/libc/math/lib_sinl.c#L77), [cos](../lib/libc/math/lib_cos.c#L58), [cosf](../lib/libc/math/lib_cosf.c#L55), [cosl](../lib/libc/math/lib_cosl.c#L58), [tan](../lib/libc/math/lib_tan.c#L58), [tanf](../lib/libc/math/lib_tanf.c#L55), [tanl](../lib/libc/math/lib_tanl.c#L58), [sqrt](../lib/libc/math/lib_sqrt.c#L61), [sqrtf](../lib/libc/math/lib_sqrtf.c#L58), [sqrtl](../lib/libc/math/lib_sqrtl.c#L61), [pow](../lib/libc/math/lib_pow.c#L65), [powf](../lib/libc/math/lib_powf.c#L55), [powl](../lib/libc/math/lib_powl.c#L58), [exp](../lib/libc/math/lib_exp.c#L88), [expf](../lib/libc/math/lib_expf.c#L76), [expl](../lib/libc/math/lib_expl.c#L88), [log](../lib/libc/math/lib_log.c#L76), [logf](../lib/libc/math/lib_logf.c#L70), [logl](../lib/libc/math/lib_logl.c#L71), [floor](../lib/libc/math/lib_floor.c#L58), [ceil](../lib/libc/math/lib_ceil.c#L58), [round](../lib/libc/math/lib_round.c#L41), [trunc](../lib/libc/math/lib_trunc.c#L60), [fabs](../lib/libc/math/lib_fabs.c#L58), [fmod](../lib/libc/math/lib_fmod.c#L58), [isnan](../os/include/tinyara/math.h#L124), [isinf](../os/include/tinyara/math.h#L125), [isfinite](../os/include/tinyara/math.h#L126) |
| 기타 | [_Exit](../os/include/stdlib.h#L321), [abort](../lib/libc/stdlib/lib_abort.c#L115), [basename](../lib/libc/libgen/lib_basename.c#L98), [dirname](../lib/libc/libgen/lib_dirname.c#L98), [getopt](../lib/libc/unistd/lib_getopt.c#L131), [swab](../lib/libc/unistd/lib_swab.c#L83), [mkstemp](../lib/libc/stdlib/lib_mkstemp.c#L113), [setlocale](../lib/libc/locale/lib_setlocale.c#L81), [localeconv](../lib/libc/locale/lib_localeconv.c#L80), [syslog](../lib/libc/syslog/lib_syslog.c#L256), [setlogmask](../lib/libc/syslog/lib_setlogmask.c#L108), [FD_ZERO](../os/include/sys/select.h#L142), [FD_SET](../os/include/sys/select.h#L140), [FD_CLR](../os/include/sys/select.h#L139), [FD_ISSET](../os/include/sys/select.h#L141) |

### 실제 호출 관계

| API | 이 소스의 구현 |
| --- | --- |
| `sem_init` | 세마포어 구조체 초기화. [구현](../lib/libc/semaphore/sem_init.c#L109). |
| `pthread_equal` | 두 thread ID를 비교하는 매크로. [헤더](../os/include/pthread.h#L452). |
| `pthread_self` | `getpid()` 호출로 확장되는 매크로. [헤더](../os/include/pthread.h#L441). |
| `pthread_setcancelstate` | libc 래퍼에서 `task_setcancelstate` syscall을 호출. [구현](../lib/libc/pthread/pthread_setcancelstate.c#L103). |
| `pthread_rwlock_init` | 조건변수와 뮤텍스를 조합. [구현](../lib/libc/pthread/pthread_rwlock.c#L87). |
| `pthread_barrier_wait` | 내부 세마포어의 `sem_wait`·`sem_post`를 사용. [구현](../lib/libc/pthread/pthread_barrierwait.c#L159). |
| `fopen` | `open`과 `fs_fdopen` syscall을 호출. [구현](../lib/libc/stdio/lib_fopen.c#L277). |
| `readv`·`writev` | 각 버퍼에 대해 `read`·`write`를 호출하는 libc 구현. [readv](../lib/libc/uio/lib_readv.c#L116), [writev](../lib/libc/uio/lib_writev.c#L130). |
| `sleep` | `nanosleep`을 이용. [구현](../lib/libc/unistd/lib_sleep.c#L156). |
| `signal` | `sigaction`을 이용. [구현](../lib/libc/signal/signal.c#L124). |
| `creat`·`fdatasync`·`_Exit` | 각각 `open`·`fsync`·`_exit`로 확장되는 매크로. [creat](../os/include/fcntl.h#L163), [fdatasync](../os/include/unistd.h#L118), [_Exit](../os/include/stdlib.h#L321). |
| `getaddrinfo` | 소켓을 만들고 ioctl 요청으로 주소 조회를 전달. [구현](../lib/libc/netdb/lib_getaddrinfo.c#L64). |

### 표준 버전 및 구현 제약

- `strlcpy`, `asprintf`, `vasprintf`는 이 트리에 구현되어 있고 **POSIX 2024에서 새로 표준화**됐다. POSIX 2017 목록에는 없다. [2024 추가 목록](https://pubs.opengroup.org/onlinepubs/9799919799/xrat/V4_xsh_chap01.html), [strlcpy 구현](../lib/libc/string/lib_strlcpy.c), [asprintf 구현](../lib/libc/stdio/lib_asprintf.c), [vasprintf 구현](../lib/libc/stdio/lib_vasprintf.c).
- `usleep`, `pthread_yield`, `inet_aton`은 TizenRT에 이름이 있어도 POSIX 2017·2024의 표준 API로 분류하면 안 된다. `usleep`은 과거 표준에 있었지만 Issue 7에서 제거됐다. [2017 변경 내역](https://pubs.opengroup.org/onlinepubs/9699919799/xrat/V4_xsh_chap01.html).
- `access` 구현은 현재 인자를 검사하지 않고 0을 반환하는 stub이다. 표준 이름이 있다는 사실을 접근 권한 검사 지원으로 보지 않는다. [구현](../lib/libc/unistd/lib_access.c#L106).
- `pthread_condattr_setclock`는 `CONFIG_ENABLE_IOTIVITY`일 때 빌드되지만 clock ID를 검증한 뒤 속성에 저장하지 않고 성공을 반환한다. 공개 `pthread.h` 선언도 확인되지 않아 위 제공 목록에서 제외했다. [빌드 조건](../lib/libc/pthread/Make.defs#L81), [구현](../lib/libc/pthread/pthread_condattrsetclock.c#L96).
- `pause`는 [커널 구현](../os/kernel/signal/sig_pause.c#L112)이 있으나 syscall 등록표에는 없다. 따라서 libc 래퍼 예시로 분류하지 않았으며, protected build에서 앱 호출 가능 여부를 별도로 확인해야 한다.
- `truncate`는 [소스 구현](../lib/libc/unistd/lib_truncate.c#L129)과 빌드 등록이 있지만 공개 `unistd.h` 선언이 확인되지 않아 주요 제공 목록에서 제외했다.
- `munmap`은 설정에 따라 빈 매크로로 정의된다. `tcdrain`, `tcflow`, `tcgetsid`, `tcsendbreak` 등은 공개 헤더 선언만으로 구현 제공을 판단하지 않았다. [munmap 헤더](../os/include/sys/mman.h#L179), [termios 헤더](../os/include/termios.h).
- libc/헤더 구현을 확인한 소스 조사이며, 현재 보드의 빌드·실행 성공이나 POSIX 전체 동작 적합성을 시험한 결과는 아니다. `lib/libc/libc.csv`는 이 커밋에서도 NASCA DRM 바이너리이므로 해당 파일을 텍스트 API 목록으로 사용하지 않았다.
