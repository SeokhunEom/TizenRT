# 3단계: 앱용 ioctl 드라이버

전제: 2단계가 확인·커밋됐고 사용자가 3단계 시작을 지시했다. [공통 진행 방식](README.md)과 [설계 문서](../HealthMonitorImplementationPlan.md)의 3절을 사용한다.

## 목표

앱에서 전용 장치를 open하고 ioctl로 자신의 감시 상태를 조작할 수 있게 한다.

## 구현 범위

1. 전용 character driver와 조건부 장치 등록을 추가한다.
2. START·KICK·STOP을 2단계 내부 함수에 직접 연결한다.
3. 호출 스레드 자신을 대상으로 처리하고, fd별 동적 감시 상태를 만들지 않는다.
4. open은 장치 접근, close는 fd 정리로 유지한다. 감시 해제는 STOP과 스레드 종료에서 처리한다.
5. 앱 wrapper가 필요하면 승인된 호출 규약을 감싸는 최소 범위로 추가한다.

예상 변경 위치: driver, 공개 헤더의 필요한 보완, driver 빌드 연결, 조건부 보드 장치 등록. 새 syscall과 기존 HW watchdog upper-half의 명령 경로를 추가하지 않는다.

## 이 단계 완료 시 동작

앱 ioctl로 실제 등록 상태를 조작할 수 있다. 아직 system tick에 만료 검사를 연결하지 않았으므로 KICK을 중단해도 자동 리셋하지 않는다.

## 검증

- 앱에서 open → START → KICK → STOP → close를 실행한다.
- 같은 fd를 여러 스레드가 사용했을 때 각각 자신에게 적용되는지 확인한다.
- close가 다른 등록 스레드의 감시를 해제하지 않는지 확인한다.
- 지원하지 않는 명령과 중복·미등록 호출의 errno를 확인한다.
- ioctl 처리 경로에 새 할당, 세마포어·뮤텍스 대기가 없는지 점검한다.

## 사용자 확인 항목

- 실제 앱 호출 예가 사용하기에 적절한가?
- fd 공유와 close의 의미가 예상과 같은가?
- wrapper가 필요 이상으로 fd 관리나 자동 등록 정책을 추가하지 않았는가?

완료 기준: 실제 ioctl 전달과 호출 스레드별 상태 변경을 확인할 수 있다. 검증 환경과 자동 만료 검사 미연결 상태를 명시해 제출하고, 사용자 확인 후 커밋한다.

## 구현·검증 결과 (2026-09-16)

### 구현 범위와 호출 규약

- [전용 드라이버](../../os/drivers/health_monitor.c): `HMIOC_START/KICK/STOP`을 기존 커널 함수로 직접 전달한다. 드라이버 자체의 잠금, fd별 상태, 동적 할당, worker, 로그 또는 사용자 포인터 역참조는 없다. 감시 데이터의 동기화는 2단계 레지스트리가 담당한다.
- [driver Makefile](../../os/drivers/Makefile): `CONFIG_HEALTH_MONITOR=y`일 때만 소스를 포함하고 private kernel 헤더 검색 경로를 추가한다. 기존 TASK_MANAGER 드라이버와 같은 내부 헤더 접근 방식이며 커널의 START/KICK/STOP을 앱 공개 헤더로 옮기지 않았다.
- [RTL8730E 초기화](../../os/board/rtl8730e/src/rtl8730e_boot.c): `board_initialize()`에서 조건부로 `/dev/health_monitor`를 등록한다. 실패는 기존 `lldbg()`로 보고하고, ISR이나 ioctl에서 등록을 재시도하지 않는다. 기준 보드는 `CONFIG_BOARD_INITIALIZE=y`이며 다른 보드의 초기화는 이번 범위에 포함하지 않았다.
- [공개 헤더](../../os/include/tinyara/health_monitor.h): 명령 번호와 경로를 유지하고 O_RDWR 및 인자 규약을 명확히 했다. 추가한 `health_monitor_register()` 선언은 커널/보드 시작용이며 보호 빌드의 앱에는 노출하지 않는다.
- [HEALTH_MONITOR 설정](../../os/kernel/Kconfig): 장치 접근에 필요한 `NFILE_DESCRIPTORS > 0` 의존성을 추가했다. 별도 드라이버 설정, 앱 wrapper 또는 새 syscall은 만들지 않았다. 기준 defconfig의 기본 비활성 정책은 유지했다.

보호 빌드의 기존 호출 경로를 그대로 사용한다.

```text
앱 ioctl(fd, cmd, unsigned long arg)
  → 기존 libc 변환 및 fs_ioctl syscall
  → fs_ioctl() → fs_getfilep() → file_ioctl()
  → health_monitor_ioctl()
  → health_monitor_start() / kick() / stop()
```

장치는 `open(HEALTH_MONITOR_DEVPATH, O_RDWR)`로 연다. 이 VFS의 `inode_checkflags()`는 read/write 콜백이 없으면 O_RDWR open을 거부하므로, 두 콜백은 제공하되 실제 데이터 I/O는 `-ENOSYS`로 거부한다. open/close 콜백은 NULL이며 자체적으로 등록·해제를 수행하지 않는다.

START의 timeout은 포인터가 아닌 `unsigned long` 값으로 전달한다. 커널은 지원 timeout 밖의 값을 `-EINVAL`로 거부하며, unsigned long이 32비트보다 큰 빌드에서도 인자를 조용히 잘라내지 않는다. KICK/STOP 인자는 사용하지 않지만 기존 variadic ioctl 규약에 맞춰 반드시 `0UL`을 전달한다. 드라이버의 음수 errno는 VFS에서 앱의 `-1`/`errno`로 변환하며, 알 수 없는 명령은 `ENOTTY`다.

같은 fd를 공유해도 명령은 현재 호출 스레드에만 적용된다. 다른 fd로 STOP할 수도 있다. 마지막 fd를 닫아도 등록은 유지되므로 STOP 또는 스레드 종료가 필요하다. 앱은 호출 중인 fd를 다른 스레드가 닫거나 재사용하지 않도록 수명을 관리해야 한다.

### 메모리와 대기 경로

드라이버는 정적인 `file_operations`만 사용하며 `f_priv`나 inode의 private 상태를 만들지 않는다. 실제 ioctl → 레지스트리 경로에는 새로운 할당, 세마포어·뮤텍스 대기가 없고, 2단계의 로컬 IRQ 마스킹·전용 spinlock만 사용한다.

다만 **장치 등록과 open/close를 포함한 VFS 전체가 무할당·무잠금이라는 의미는 아니다.** 부팅 중 `register_driver()`는 기존 inode 할당·세마포어 경로를 사용하고, open/close에는 기존 fd 관리가 있다. 이들은 START/KICK/STOP ioctl의 반복 경로 밖에 있다.

신규 드라이버의 독립 ARM `-Os` 객체 기준으로 text 92 B, 읽기 전용 데이터 52 B, data/BSS 0 B다. 최종 링크 정렬·VFS inode 공간은 별도이며 레지스트리/TCB 저장공간은 2단계와 같다.

### 앱 호출 예와 보드 확인

[board_smoke.c](../../os/kernel/health_monitor/tests/board_smoke.c)를 테스트 앱 소스에 추가하고 감시할 스레드에서 `health_monitor_smoke()`를 호출한다. 이 파일은 제품에 자동 포함되지 않으며 공개 헤더만 사용한다.

1. `CONFIG_HEALTH_MONITOR=y`, `CONFIG_BOARD_INITIALIZE=y`인 RTL8730E 커널을 빌드·적용한다. 이번 작업 시작 시 현재 작업 트리의 두 옵션이 켜져 있음을 확인했지만 활성 설정 파일은 변경하지 않았다.
2. 예제는 open → `START(1000UL)` → `KICK(0UL)` → `STOP(0UL)` → close를 실행하고 0 또는 음수 errno를 반환한다.
3. START 이후 같은 스레드의 중복 START는 `EEXIST`, STOP 이후 STOP은 `ENOENT`, 미등록 KICK은 성공인지 확인한다.
4. 공유 fd를 사용하는 두 스레드를 각각 등록한 뒤 한 스레드의 STOP이 다른 스레드의 등록을 지우지 않는지 확인한다. close 후 다시 open한 동일 스레드의 START가 `EEXIST`이면 close가 감시를 해제하지 않은 것이다.

이 단계에서는 KICK을 끊어도 자동 리셋하지 않는다. 위 테스트는 장치 접근·등록 조작을 확인하며 deadline 강제 검사는 4단계에서 연결한다.

### 실행한 검증

- [호스트 테스트](../../os/kernel/health_monitor/tests/README.md)는 제품 레지스트리·드라이버와 실제 VFS open/close/file_close, fd 조회, ioctl 전달·errno 변환 소스를 함께 컴파일한다. inode/fd 저장공간 및 task/IRQ는 호스트 모델이다.
- 기존 registry와 신규 driver/VFS 테스트를 UP/SMP 각각 실행해 모두 통과했다. 등록 실패 전달, invalid/duplicate START, 미등록 KICK/STOP, unknown ioctl, 잘못된 fd, 늦은 KICK, fd 공유, 독립 STOP, 마지막 fd close 후 등록 유지·reopen, 대상 cleanup을 확인했다.
- SMP 모델에서는 두 pthread가 같은 fd로 각각 10,000회 START/KICK/STOP을 실행했다. 실제 ARM IRQ·스케줄러·SVC를 실행한 결과는 아니다.
- 최초 ASan/UBSan 실행은 통과했다. 이후 ptrace 기반 샌드박스에서 LeakSanitizer가 실행 환경 오류를 보고해, 저장한 최종 실행에서는 `ASAN_OPTIONS=detect_leaks=0`으로 leak 검사만 비활성화했다. ASan의 다른 검사와 UBSan은 유지했으며 네 테스트가 모두 통과했다.
- 커밋 직전에도 같은 설정으로 registry/driver의 UP/SMP 테스트 네 개를 재실행해 모두 통과했다.
- 기준 설정의 보호 모드 **전체 kernel/driver 아카이브**를 ON/OFF 각각 clean build했다. ON은 kernel 243개·driver 63개 객체, OFF는 242개·62개다. 보드 전체 라이브러리 대신 변경된 `rtl8730e_boot.o`를 ON/OFF 각각 컴파일했다. OFF 산출물에는 health monitor 심볼·참조가 없고 ON 보드 객체에는 `health_monitor_register` 호출이 있다.
- 기존 경고는 kernel 6개, driver 26개, 보드 초기화 객체 1개로 ON/OFF에 동일하게 남았다. 신규 드라이버는 ARM `-Wall -Wextra -Werror -Wshadow -Wundef` 독립 컴파일을 통과했다.
- 앱 smoke 예제를 `__KERNEL__` 없이 ARM C/C++로 컴파일했다. 앱 객체는 open/ioctl/close/get_errno만 참조하며 커널 private 함수에 직접 의존하지 않는다. 드라이버 객체의 외부 의존성은 START/KICK/STOP과 부팅용 `register_driver`뿐이며, ioctl 코드에 할당·대기 함수 호출이 없다.
- 검증에는 `/tmp/health-monitor-step1.isS8Pw` 빌드 사본을 재사용했다. 결과는 `/tmp/health-monitor-step3.SQdcM4`의 `host-no-lsan.log`, `kernel-on/off.log`, `drivers-on/off.log`, `board-on/off.log`, `driver-arm.log`, `undefined-symbols.log`와 객체/아카이브에 있다. 작업 트리의 활성 `.config`와 기준 defconfig는 변경하지 않았다.

재현 명령은 다음과 같다. ptrace 제한이 없는 환경에서는 `ASAN_OPTIONS` 없이 기본 leak 검사도 유지한다.

```sh
ASAN_OPTIONS=detect_leaks=0 make -C os/kernel/health_monitor/tests test
```

### 검증 제한과 사용자 확인

- 실제 보드에서 앱의 variadic ioctl → 보호 빌드 SVC → 커널 드라이버를 실행하는 검증은 남아 있다. 앱 예제는 컴파일만 확인했으며, 전체 펌웨어 링크·플래시·부팅은 이번 단계에서 수행하지 않았다.
- 기존 전체 Kconfig 파싱의 타 보드 절대경로 제한은 별도로 해결하지 않았다. 이번 NFILE_DESCRIPTORS 의존성 추가를 전체 Kconfig 실행 통과로 보고하지 않는다.
- 기존 HW watchdog 경로, syscall 표, 제품 앱의 fd 관리, tick·PANIC·PM 코드는 변경하지 않았다. 보드 초기화의 등록 오류 메시지 외에 새로운 진단 수집을 추가하지 않았다.
- read/write의 ENOSYS 정책, fd 공유·close 의미, 별도 wrapper 없이 공개 ioctl을 쓰는 형태를 포함한 결과 제출 후 사용자가 3단계 커밋을 승인했다. 4단계 구현은 시작하지 않았으며 별도 지시를 기다린다.

승인된 3단계 커밋: `health_monitor: expose task operations through ioctl`.
