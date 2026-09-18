# QEMU build_test: 4단계 C++ / libc++

검증일: 2026-09-18 KST. 브랜치: `codex/qemu-build-test`.
3단계 RAM 저장장치 작업을 `c6fc5f0f3`으로 커밋한 뒤 진행했다.

## 구성

ARMv7-M / Cortex-M3 Flat의 기존 kernel·filesystem·SmartFS 구성을 유지하고 C++11, 전역 생성자, LLVM libc++, RTTI, 예외, `helloxx`, `cxxtest`, `libcxx_utc`를 켰다. 네트워크는 다음 단계다.

- C++의 CPU 옵션을 C와 같은 `ARCHCPUFLAGS`에서 받는다. QEMU Make.defs에 남아 있던 Cortex-R4/VFPv3 옵션과 중복 언어 버전 옵션을 제거했다.
- 예외 옵션은 `LIBCXX_EXCEPTION`에 맞춘다. `libsupc++.a`는 동일 Cortex/Thumb/float ABI 옵션으로 GCC multilib 경로를 조회해 링크한다.
- libc++에 필요한 C99 bool, wchar, locale, monotonic clock, recursive/errorcheck mutex 지원을 켰다.
- `/dev/urandom`의 XORSHIFT128은 random_device TC용 의사난수 소스다. 암호학적 엔트로피 검증은 아니다.
- UTC는 algorithms, containers(map/vector/list 포함), utilities, diagnostics, numerics, stdbool, thread, iterators, strings를 포함한다. 별도 `STRINGS_LARGESIZE` 옵션은 16 MiB RAM 대상에서 제외했다.
- 기존 `PTHREAD_MUTEX_ROBUST=y`는 유지했다. C++ 예외를 실행하는 std::thread를 위해 기본 pthread stack을 1024에서 4096 bytes로 늘렸다.

## 단계별로 발견한 문제

1. libc++ 없이 HAVE_CXX만 켠 초기 시도는 기존 libxx Makefile이 없는 `libxx_delete_sized.cxx`를 참조해 실패했다. 이번 full-set은 libc++ 경로를 사용하며, bare libxx 경로는 수정·검증하지 않았다.
2. libc++의 bool/chrono/wchar 의존성과 cxxtest의 locale 링크 의존성을 각각 설정으로 충족했다. 기본 helloxx와 확장 cxxtest를 순서대로 실행했다.
3. UTC 추가 후 TASH 없이 testcase 등록 메시지만 출력됐다. GDB에서 `appinit -> up_cxxinitialize -> recursive_mutex constructor -> __throw_system_error(ENOSYS) -> terminate -> abort -> exit`를 확인했다. `PTHREAD_MUTEX_TYPES=y`로 해결했다.
4. UTC mutex 시험에서 일반 task의 robust mutex 추적이 pthread 전용 TCB의 `mhead`에 접근했다. 당시 `sizeof(task_tcb_s)=272`, pthread의 `mhead` offset은 `0x110`(272)이어서 task 할당 범위를 벗어났다. 실제 QEMU heap corruption도 관찰했다.

`mhead`를 공통 `tcb_s`로 이동하고 mutex lock/unlock과 소유자 종료 처리에서 공통 TCB를 사용하도록 했다. `task_recover`는 하나의 critical section 안에서 대기 count 취소 → mutex inconsistent 표시 및 게시 → 남은 semaphore holder 해제 순서로 정리한다. pthread exit/cancel의 기존 회수 뒤 다시 호출되어도 빈 목록은 그대로 종료한다. 일반 task 생성·두 mutex의 비-LIFO unlock·trylock·정상 종료·지연 삭제·비동기 삭제·자신의 NORMAL mutex에 재잠금 대기 중 비동기 삭제·EOWNERDEAD 및 복구 후 재잠금을 kernel TC에 추가했다.

독립 리뷰가 지적한 초기 정리 순서의 문제도 실제 QEMU로 재현했다. 삭제 대상의 wait count를 취소하기 전에 자신이 소유한 mutex를 게시하면 count와 wait list가 맞지 않아 `sem_holder.c:926`에서 assert했다. 단독 TC는 수정 전 2.553초에 assert, 순서 수정 후 1 PASS / 0 FAIL(9.315초)이었다. 기존 `pthread_cancel`의 별도 조기 mutex 게시 경로까지 수정했다고 주장하지 않는다.

설정 변경 중 incremental build가 일부 libc++ UTC 객체를 재컴파일하지 않아 서로 다른 pthread ABI가 섞인 실행도 관찰했다. 최종 검증은 `make distclean`부터 전체를 다시 빌드한 firmware로 수행했다. TCB layout 변경 역시 전체 재빌드가 필요하다.

1 KiB pthread 구성에서 `thread_lock_unique_locking_lock`의 예외 경로가 stack을 소진했다. QEMU dump는 사용량 1016/1020 bytes와 stack 바로 아래 free node 손상을 보였다. 해당 단계의 ELF/config와 로그를 보존하고 4 KiB 구성으로 다시 clean build했다.

## 통합 회귀에서 발견한 SmartFS 덮어쓰기 결함

C++ UTC가 통과한 뒤 기존 storage-smoke를 실행했을 때 `smart`가 EIO를 보고했다. FS error log는 블록 111에 free count가 남아 있지만 실제로 할당할 수 있는 섹터가 없다고 보고했다. 처음부터 EIO를 ENOSPC로 바꾸지 않고 실패 ELF를 고정해 추적했다.

GDB로 물리 섹터 446의 sequence 바이트가 `0xff`에서 `0xdf`로 바뀜을 확인했다. 일반 RAM watchpoint만으로 잡히지 않아 SRAM bit-band alias도 추적하자 `smart_seek_with_write_test`의 배열 접근에서 변경됐다. 실제 배열 인덱스는 48,788,526이었다. 손상된 문자열로 `rand() % len`을 수행한 결과가 RAM 범위를 벗어난 쓰기와 MTD 데이터 손상으로 이어졌다.

`smart_test`에 길이·fseek/fread/fwrite/fflush 결과 검사를 추가하자 문제를 앞에서 검출했다. line 2의 실제 문자열 길이는 9, 기대 길이는 28이었다. 기존 코드는 I/O 결과를 확인하지 않았고 실패해도 OK를 반환했다. 실패하면 ERROR를 반환하도록 고쳤다.

원인은 `smartfs_write`의 **sector buffer를 사용하지 않는 덮어쓰기 경로**였다. 함수가 요청과 EOF에 맞춰 `bytes`를 계산했지만, BIOC_WRITESECT에는 `availbytes - curroffset`을 전달했다. 짧은 요청에서도 caller 버퍼 밖의 데이터를 읽어 섹터의 나머지를 덮었다. 이 경로의 count만 `bytes`로 수정했다. 전체 캐시를 제공하는 sector-buffer 경로는 그대로다.

새 ITC는 16바이트 부분 덮어쓰기와 섹터 경계를 가로지르는 32바이트 덮어쓰기를 실행하고 close/reopen 후 파일 전체를 비교한다. 요청 버퍼에 충분한 backing memory를 줘 수정 전에도 잘못된 메모리를 읽지 않고 주변 파일 데이터의 변경으로 검출한다. 수정 전 ITC-only 실행은 32 PASS / 1 FAIL이며, 실패 항목은 `preserve surrounding bytes`다. 3단계의 기존 `smart_test` Pass만으로 검증되지 않았던 인접 데이터 보존을 명시적으로 검증한다.

## 검증 결과

최종 clean build가 통과했다. 실제 `.config`는 build_test defconfig와 byte 단위로 같다. 아래 검사는 동일 ELF를 각각 새 QEMU에서 실행했다.

| 검사 | 결과 | 확인 내용 |
|---|---|---|
| C++ smoke | PASS, 32.736초 | helloxx의 3종 생성자, C++11, cxxtest RTTI/예외/STL/iostream/std::thread, task 종료 |
| libc++ UTC | PASS 795 / FAIL 0, 27.211초 | 활성화한 모든 일반 UTC 그룹, `/mnt` sentinel 보존 |
| kernel TC | PASS 433 / FAIL 0, 513.482초 | 4-mode mutex 회귀 포함, `/mnt` sentinel 보존 |
| filesystem TC | PASS 203 / FAIL 0, 72.895초 | 새 덮어쓰기 보존 TC 포함, `/mnt` sentinel 보존 |
| SmartFS smoke | PASS, 69.729초 | seek 50, seek/write 50, circular log 32, fill/delete 100회, 각 회차 파일 수 최소 213, remount/sentinel 보존 |
| drivers TC | PASS 14 / FAIL 8, 8.621초 | loop/BCH 정상, 기존 PWM 3·watchdog 3·ADC 2 미지원 실패 유지, sentinel 보존 |

일반 filesystem API 시험은 202에서 203 PASS로 늘었다. 기존 driver 실패를 SKIP이나 전체 PASS로 바꾸지 않았다. C++ 및 filesystem 검증과 이 하드웨어 미지원 결과를 구분한다.

- ELF SHA-256: `8563a2e90a66cee6926882f165219677de2014c9852c44c044864d39b03ff1e3`
- BIN SHA-256: `48320fefadc76fda94bfd534a5f1f3a0e345bb7a04efcfa043815e5a75fd2652`
- defconfig / `.config` SHA-256: `2f8740d76c0e636436e5608a0a3bd261f62122e72357725bb8e81b6883c9acd7`
- ELF text/data/bss: 2,314,442 / 1,176 / 2,211,908 bytes. BIN: 2,315,620 bytes.
- ELF attributes: ARMv7 Microcontroller / Thumb-2, wchar 4 bytes. ABI library: `thumb/v7-m/nofp/libsupc++.a`.
- `git diff --check`, Python AST 구문 검사 통과. 전체 Kconfig 정규화 제한은 [3단계](QEMU_Build_Test_Stage03.md)의 기존 제한과 같다.
- 임시 kernel/filesystem entrypoint 변경, diagnostic FS log 및 2회 stress 옵션은 최종 소스·설정에 남기지 않았다.

로컬 원본 증거는 ignored `tmp/qemu-build-test-stage04/`에 보존한다. `verified-*`는 최종 실행 결과다. `final.elf`, `final.bin`, `final.config`, `manifest.json`, `final-source.patch`, `build-final.log`, `final-elf-info.txt`가 실행 대상과 빌드 증거다. 실패한 중간 시도도 별도 디렉터리에 보존하며 성공 증거로 집계하지 않는다.

## 재현

환경은 [0~1단계](QEMU_Build_Test_Stage01.md)의 ARM64 Docker 이미지와 패치된 QEMU 2.12.0을 사용한다.

```sh
docker run --rm --pull=never --platform linux/arm64 \
  -v "$PWD:/work" -w /work/os tizenrt/tizenrt:2.0.1-arm64-local \
  bash -c 'set -e; make distclean; cd tools; ./configure.sh qemu/build_test; cd ..; make -j4'
python3 tools/qemu-build-test/cpp-smoke.py --root . \
  --output tmp/qemu-build-test-stage04/cpp-new
python3 tools/qemu-build-test/run-testcases.py --root . \
  --suite libcxx_utc --check-storage --timeout 1200 \
  --output tmp/qemu-build-test-stage04/utc-new
```

다른 회귀 검사는 같은 runner의 `--suite kernel_tc`, `--suite filesystem_tc`, `--suite drivers_tc`에 `--check-storage`를 사용한다. SmartFS stress는 `storage-smoke.py --root . --output tmp/qemu-build-test-stage04/smartfs-new`로 실행한다. Kernel timeout은 1500초를 사용했다.

`cpp-smoke.py`는 static/stack/dynamic 생성자, 언어 버전, RTTI, 예외 catch, STL, iostream, std::thread와 join 출력을 확인하고 task 종료도 확인한다. UTC runner는 비동기 TASH 프롬프트가 아니라 suite 종료 집계와 fail count로 판정한다.

CircleCI 변경·실행, 물리 보드 시험, 원격 push는 이 단계에 포함하지 않았다. 검증 범위는 위 QEMU ARMv7-M Flat 구성이다.

## 리뷰 및 검증 경계

`code-review` 스킬의 Standards/Spec 두 축에서 `c6fc5f0f3` 이후 작업 diff를 독립적으로 검토했다. 이번 작업의 기준과 요구사항은 사용자 대화와 단계 문서이며 별도 issue tracker는 사용하지 않았다. 작업 중인 변경이므로 비교는 `git diff c6fc5f0f3`와 새 파일을 대상으로 했다.

- Standards: 문서화된 규칙 위반 없음. C++ smoke 시작 처리에 관한 P3 지적은 Popen/selector 등록을 try 내부로 이동하고 프로세스 유무에 따라 정리하도록 수정했다. Docker 실행 파일을 찾지 못하는 경우에도 실패 result.json이 생성됨을 직접 확인했다.
- Spec: task 삭제 정리 순서의 P2 1건을 QEMU red/green으로 수정했다. SmartFS의 bounded count 수정과 회귀 TC도 별도로 검토했다.
- 최종 소스 재검토에서 Standards/Spec 모두 미해결 지적 0건이었다. 이 리뷰 결과와 runtime 증거는 별도로 구분한다.
- 공통 TCB, mutex 정리 및 SmartFS 비버퍼 쓰기를 수정했으므로 다른 보드/SMP/보호 빌드의 실행 검증과 동일시하지 않는다.
- 큰 문자열 전용 UTC, bare libxx, 네트워크, 물리 flash 전원 차단 시험은 이번 범위에 포함하지 않는다.
