# QEMU build_test: 2단계 baseline 테스트 결과

검증일: 2026-09-18 KST. 브랜치: `codex/qemu-build-test`.
먼저 0~1단계를 `988dbe77e766890f60cd8a9d2e536f760aa96111`로 커밋한 뒤 실행했다.
이 문서는 현재 활성화된 테스트의 실행 결과와 미충족 전제조건을 기록한다. 전체 테스트 통과 상태는 아니다.

## 실행 대상

[0~1단계](QEMU_Build_Test_Stage01.md)의 동일 ELF를 사용했다. 각 suite마다 새 QEMU를 시작해 테스트 간 장치·마운트·전역 카운터 영향을 분리했다.
이번 단계에는 OS/앱 소스 및 defconfig를 변경하지 않았다. 새 검사 스크립트와 결과 문서를 추가했다.

- ELF SHA-256: `326afdcaae4b8bd719eab7c80aa1bd81498b6241e02df50d2c52bdea8ab9dc1e`
- defconfig SHA-256: `fa309a2dc89df90eb4ec790d58c844bd8f15be4e6ab7ff3c3a6b1d54418d7780`
- QEMU 이미지: `tizenrt/qemu-build-test:2.12.0-16m`
- 이미지 ID: `sha256:58b19f728ea2a5bf146b2ed1919472aa0f263007f6f13b8d2654a36a0920daa8`
- 실행 모델: LM3S6965EVB / Cortex-M3 / ARMv7-M Flat / SRAM 16MiB

## 결과

| 검사 | 완료 | 결과 | 해석 |
|---|---|---|---|
| `kernel_tc` | 아니오 | 206개 PASS 출력 후 600초 제한시간 초과 | 최종 Kernel TC End가 없어 suite 실패. 이후 테스트는 미실행 |
| `kernel_tc` 진단 재실행 | 아니오 | 같은 206개 PASS 후 90초 제한시간 초과 | 소스 수정 없이 동일 위치 재현, GDB 및 셸 상태 수집 |
| `drivers_tc` | 예 | PASS 2 / FAIL 20 | 두 번의 새 부팅에서 같은 결과 |
| `filesystem_tc` | 예 | PASS 98 / FAIL 193 | 최종 요약 확인. 파일시스템 준비 부족에 따른 연쇄 실패 포함 |
| `smart` | 명령 반환 | 파일 생성·디렉터리 접근 실패 | 100회 루프, 오류 출력 300줄. 반환만으로 성공 처리하면 안 됨 |
| `smart_test -s 5 -w 5 -l 10 /mnt/stage02` | 명령 반환 | `Unable to create file /mnt/stage02` | seek/write 검사에 필요한 파일 생성부터 실패 |

PASS/FAIL 값은 suite 자체 카운터다. 파일시스템의 193 FAIL에는 `vfs_unmount` 69회, `vfs_mount` 20회의 반복 helper 실패가 포함된다. 193개의 독립적인 결함으로 해석하지 않는다. 커널의 206은 부분 실행 로그 수이며 최종 suite 집계가 아니다. 실제 FAIL을 SKIP으로 바꾸지 않았다.

## 현재 장치·마운트

새 부팅의 `ls /dev`:

```text
console cpuload mminfo null os_api_test pm taskmgr ttyS0 zero
```

`mount`:

```text
/proc type procfs
/tmp type tmpfs
```

`/dev/pwm0`, `/dev/watchdog0`, `/dev/adc0`, `/dev/mtdblock1`, `/dev/smart1`이 없다. `/mnt`와 `/fsmnt`에 사용 가능한 SmartFS도 없다. 이는 이번 펌웨어의 실제 상태이며 QEMU의 모든 구현 가능성을 판정한 것은 아니다.

## 드라이버 실패 분류

| 분류 | PASS | FAIL | 확인된 전제조건 |
|---|---:|---:|---|
| null | 1 | 0 | `/dev/null` 존재 |
| zero | 1 | 0 | `/dev/zero` 존재 |
| PWM | 0 | 3 | `/dev/pwm0` 없음 |
| watchdog | 0 | 3 | `/dev/watchdog0` 없음 |
| ADC | 0 | 2 | `/dev/adc0` 없음 |
| loop | 0 | 6 | `/mnt/loopfile` 생성 실패 후 loop 장치 관련 검사 실패 |
| BCH | 0 | 6 | `/dev/mtdblock1` 없음, BCH 장치 등록부터 실패 |

근거: `apps/examples/testcase/le_tc/drivers/tc_pwm.c`, `tc_watchdog.c`, `tc_adc.c`, `tc_loop.c`, `tc_bch.c`의 실제 장치 경로와 실패 라인을 runtime `/dev` 목록에 대조했다.
`CONFIG_DISABLE_MANUAL_TESTCASE=y`가 이 장치 의존 테스트들을 자동 제외하지는 않는다.

## 파일시스템 실패 분류

`fs_main.c`는 `AUTOMOUNT_USERFS`가 없는 현재 설정에서 `/dev/smart1`을 `/fsmnt/`에 마운트하려고 한다. 이 장치가 없어 mount 실패 뒤 open/read/write/stdio/마운트 관련 실패가 이어진다. ITC도 SmartFS 저장장치를 전제로 한다.

98 PASS에는 부적절한 인자 검사, 가상 파일시스템 일부 동작, FIFO, ProcFS, message queue, RAM disk, 메모리 stream 등이 포함된다. SmartFS 기반 저장장치 검증 통과를 뜻하지 않는다.

저장장치 전제조건을 복구한 뒤 잔여 FAIL을 다시 분류해야 한다. 모든 193 FAIL이 마운트 수정만으로 사라진다고 확정하지 않는다.

`smart`는 기본 100회 루프를 실행하지만 파일 수가 0이고 `/mnt` 접근 오류가 반복된다. `smart_test`도 실제 파일을 만들지 못한다. stage 3에는 MTD 장치 번호와 각 테스트의 마운트 경로를 함께 맞춰야 한다.

## 커널 미완료 진단

마지막 PASS는 `tc_libc_pthread_pthread_rwlock_rdlock_wrlock`이다. 소스상 다음 호출은 `tc_libc_pthread_pthread_rwlock_timedwrlock_timedrdlock`이며 두 pthread를 만들고 join한다. 해당 테스트의 timed lock 제한은 각각 2초다.

600초 실행과 90초 진단 실행 모두 같은 마지막 PASS 뒤 최종 요약이 없다. 진단 실행은 제한시간이 지난 뒤에만 GDB로 멈춰 상태를 읽었으므로 GDB가 최초 정지를 만든 것은 아니다.

GDB/셸 관찰:

- CPU는 `os_start()`의 idle loop에 있다.
- `kernel_tc` PID 7과 두 pthread PID 10/11은 `WAITSEM` 상태다.
- 스냅샷의 활성 watchdog은 TASH PID 5용 `sem_timeout` 하나다.
- GDB detach 후 `ps`, `uptime`, `free`에 응답한다. OS 전체 정지와 구분된다.
- 진단 시 `g_system_timer=2256`, `uptime=22.57`이 관찰됐다. 제한시간은 호스트 시간이다. 에뮬레이터 시간과 호스트 시간이 다르므로 이후 타이밍 검사도 guest tick을 함께 봐야 한다.

이 증거는 timed rwlock/condition wait/시험 스레드 동기화 구간의 진행 중단을 좁혀 준다. 정확한 결함 위치나 수정은 아직 확정하지 않았다. 별도 재현·진단을 먼저 해야 한다.

## 설정과 실행 노출의 차이

현재 TASH에는 세 TC suite와 `smart`, `smart_test`, `hello`, `timer`, `ramtest`가 보인다. 이번 단계의 정식 suite 실행 범위는 kernel/drivers/filesystem이며 SmartFS 명령은 전제조건 확인용으로 실행했다.

- `CONFIG_EXAMPLES_KERNEL_SAMPLE`, `CONFIG_EXAMPLES_PROC_TEST`, `CONFIG_FILESYSTEM_HELPER_ENABLE`는 defconfig에 남아 있으나 현재 관련 Kconfig/Make 분기에서 찾지 못했다.
- 파일시스템 helper의 현재 Make.defs 조건은 `CONFIG_FILESYSTEM_TEST`이다.
- EEPROM 예제는 플래그가 켜져 있어도 현재 Makefile에 TASH REGISTER가 없고 실제 명령 목록에도 없다.
- 따라서 해당 샘플·helper를 runtime 통과 항목에 포함하지 않는다. 이후 full set 정리 시 실제 빌드 및 명령 노출을 다시 맞춰야 한다.
- C++와 네트워크는 원래 baseline에서 비활성화되어 이번 단계에 실행하지 않았다.

## 검사 스크립트와 재현

`tools/qemu-build-test/run-testcases.py`는 각 실행마다 새 QEMU와 읽기 전용 펌웨어 mount를 사용한다. TC 준비 메시지를 기다린 뒤 `help`, `ls /dev`, `mount`, `free`로 전제조건을 남긴다.

TC 명령은 비동기이므로 TASH 프롬프트가 아닌 `... TC End [PASS : N, FAIL : M]`를 기다린다. N > 0, M == 0이고 후속 셸 응답이 있어야 성공한다. 누락된 종료 요약, target fault, 프로세스 조기 종료, timeout은 실패한다.

worktree 루트에서 각각 실행한다. 이번 baseline은 실제 실패가 있으므로 명령의 종료 코드 1이 정상적인 관찰 결과다.

```sh
python3 tools/qemu-build-test/run-testcases.py --root . \
  --suite kernel_tc --timeout 600 --output tmp/qemu-build-test-stage02/kernel-new
python3 tools/qemu-build-test/run-testcases.py --root . \
  --suite drivers_tc --timeout 180 --output tmp/qemu-build-test-stage02/drivers-new
python3 tools/qemu-build-test/run-testcases.py --root . \
  --suite filesystem_tc --timeout 180 --output tmp/qemu-build-test-stage02/filesystem-new
```

소스 수정 없이 GDB 진단을 반복하려면 로컬 증거 폴더의 `diagnose-kernel.py`와 `diagnose.gdb`를 사용한다. GDB 서버는 컨테이너 내부 loopback에만 열며 호스트 포트는 공개하지 않는다. SmartFS 전제조건 probe도 같은 폴더의 `probe-smartfs.py`에 보존했다.

## 증거 위치와 다음 순서

`tmp/qemu-build-test-stage02/`:

- `kernel/`: 600초 제한 실행 serial/result
- `kernel-diagnostic/`: 90초 재현 serial/result 및 `gdb.txt`
- `drivers/`, `drivers-repeat/`: 동일 PASS 2 / FAIL 20 결과
- `filesystem/`: PASS 98 / FAIL 193과 실패 라인
- `smartfs-probes/`: SmartFS 명령 원문
- 각 suite 폴더의 `pass-functions.txt`, `failure-lines.txt`: 원문에서 추출한 항목
- `manifest.json`: 동일 펌웨어·설정 해시, 최종/부분 집계

다음에는 커널 timed rwlock 진행 중단을 먼저 진단해 기준 kernel suite가 끝까지 실행되도록 해야 한다. 이후 stage 3에서 RAMMTD/SmartFS 장치와 마운트를 준비하고 storage 의존 FAIL을 재검증한다. PWM/watchdog/ADC는 현재 가상 보드 포트가 제공하는 장치 범위를 확인해 별도 검증 방식을 결정한다.
