# QEMU build_test: 3단계 RAMMTD / SmartFS

검증일: 2026-09-18 KST. 브랜치: `codex/qemu-build-test`.
먼저 [condition timeout 정지 수정](QEMU_Build_Test_CondTimeout_Fix.md)을 `edba65a85`로 커밋한 뒤 진행했다.

## 구성과 범위

기존 build_test는 RAMMTD, SMART, SmartFS 코드를 이미 빌드했지만 보드 초기화가 꺼져 있어 실제 저장장치가 없었다. 이번 단계는 장치 초기화와 테스트의 실행 전제조건을 연결한다.

| 장치 | 크기 | 용도 | 부팅 상태 |
|---|---:|---|---|
| `/dev/smart0` | 1 MiB | `smart`, `smart_test`, loop backing file 및 일반 파일 | SmartFS로 포맷 후 `/mnt` 마운트 |
| `/dev/smart1` | 1 MiB | filesystem suite의 포맷·마운트 시험 | SmartFS로 포맷, suite가 `/fsmnt/`를 직접 관리 |
| `/dev/mtdblock1` | 64 KiB | BCH raw block read/write | 독립 RAMMTD에 FTL 등록 |

세 backing store를 분리해 filesystem 포맷이나 BCH raw write가 `/mnt` 데이터를 바꾸지 않도록 했다. 총 2,162,688 bytes의 정적 RAM을 사용한다. 부팅 후 heap total은 14,589,424 bytes, suite 시작 전 used 38,976 bytes였다.

새 `CONFIG_QEMU_TEST_STORAGE`는 기본값 n이며 QEMU의 증가된 메모리 구성, RAMMTD, SMART 및 단일 root SmartFS에서만 선택할 수 있다. 선택하면 BOARD_INITIALIZE와 MTD_FTL을 활성화한다. `build/configs/qemu/build_test/defconfig`에 이 세 옵션을 반영했다. 다른 QEMU defconfig는 변경하지 않았다.

RAMMTD는 부팅마다 초기화되는 휘발성 장치다. 재마운트 데이터 보존은 검증하지만, 재부팅 영속성·실제 NOR/NAND 특성·전원 차단 복구를 검증한 것은 아니다.

## 테스트 실행 중 확인하고 수정한 문제

### kernel TC 기대값

`CONFIG_CANCELLATION_POINTS=y`에서 `pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED)`는 성공을 반환한다. 기존 TC가 ENOSYS를 기대해 발생한 1건을 OK로 바로잡았다. OS의 취소 동작은 변경하지 않았다. 저장장치 추가 전 전체 kernel suite는 432 PASS / 0 FAIL, 513.371초로 완료했다.

### filesystem ITC의 application mount 간섭

초기 장치 연결만으로 filesystem suite는 202 PASS / 0 FAIL이었다. 그러나 별도 sentinel 검사에서 `/mnt`의 보존용 파일을 찾지 못했다. 기존 ITC가 `/mnt`를 unmount하고 `/dev/smart1`로 바꾸기 때문이다.

VFS TC와 ITC가 공통 `FS_TC_MOUNT_DIR` (`/fsmnt/`)를 사용하도록 했다. ITC가 첫 unmount 검사에 필요한 mount를 직접 준비하고 마지막에 정리한다. 수정 후 동일 sentinel 검사가 통과했다. 다른 mount-operation TC의 `/smartfs_test`, `/tmpfs_test`, `/rom`도 기존대로 suite 내부에서 관리한다.

### SmartFS stress 파일명 충돌

`smart`는 무작위 이름으로 O_EXCL open한다. 최초 100회 실행에서는 EEXIST 오류 100줄을 출력했다. 이름 충돌일 때만 최대 `CONFIG_EXAMPLES_SMART_MAXOPEN`회 재시도하도록 했다. 파일 데이터는 이름 생성과 open이 성공한 뒤 생성한다. 재시도마다 긴 데이터까지 다시 생성하는 방식은 현재 난수열에서 충돌이 반복되어 사용하지 않았다. 다른 open/write 오류는 기존처럼 보고한다.

### SMART 논리 섹터 소진 반환값

용량을 채우는 stress에서 실제 write가 EIO로 실패했다. 5회 재현에 기존 FS error log를 켜자 다음 경로를 확인했다.

```text
smart_allocsector: No free logical sector numbers!  Free sectors = 9
smartfs_append_data: Error allocating new sector, ret : -5
ERROR: Failed to write file: 5
```

물리 free sector가 남아도 할당 가능한 논리 번호가 모두 사용 중이면 새 파일 데이터를 할당할 수 없다. 전체 논리 번호를 검색한 뒤 미할당 번호를 찾지 못한 분기에서 EIO 대신 ENOSPC를 반환하도록 수정했다. 다른 I/O 오류 경로는 변경하지 않았다.

수정 후 진단 로그는 ENOSPC(-28)를 보여 주었고, 진단 옵션을 제거한 원래 100회 구성에서 fill/delete·내용 검증이 모두 통과했다. `smart`는 오류가 있어도 명령이 반환될 수 있으므로 프롬프트 복귀만으로 PASS를 판단하지 않는다.

## 최종 검증

[0~1단계](QEMU_Build_Test_Stage01.md)의 ARM64 빌드 이미지와 QEMU 2.12.0 + 16 MiB 패치를 사용했다. 모델은 LM3S6965EVB / Cortex-M3 / ARMv7-M Flat이다. 최종 `.config`는 수정한 build_test defconfig와 byte 단위로 동일하다.

| 검사 | 결과 | 부가 확인 |
|---|---|---|
| clean build + 부팅 | PASS | `help`, `ps`, `free` 정상 |
| kernel TC | PASS 432 / FAIL 0, 513.432초 | 저장장치 포함 구성, sentinel 보존 |
| filesystem TC | PASS 202 / FAIL 0, 72.822초 | suite 종료 후 `/mnt` sentinel 보존 |
| drivers TC | PASS 14 / FAIL 8, 8.468초 | loop/BCH 12개 모두 PASS, sentinel 보존 |
| SmartFS smoke | PASS, 64.264초 | 아래 데이터·stress 검사 모두 통과 |

SmartFS smoke:

- 파일 쓰기/읽기와 unmount/remount 후 동일 내용 확인
- 100줄 파일에 random seek 50회, seek/write 50회
- circular log record update 32회, 명시적 Pass 출력 확인
- 기존 `smart` fill/delete 100회, 각 회차의 파일 수가 0보다 큼 (관찰 최소 213)
- 최종 완료 메시지, error 출력 없음, 별도 sentinel 파일 보존 확인

드라이버 FAIL 8개는 PWM 3, watchdog 3, ADC 2이며 해당 device node가 없다. 기존 실패를 SKIP으로 바꾸거나 전체 driver suite를 PASS 처리하지 않았다. 저장장치 전제조건 복구로 기존 PASS 2 / FAIL 20에서 loop/BCH 12건이 통과로 바뀌었다. 이들 하드웨어 장치의 가상화는 이번 단계에 포함하지 않는다.

- ELF SHA-256: `be7b7c72908416c1090a1e54e541c8134203093b82c405e61f439e6b08eb8b23`
- defconfig / 실제 `.config` SHA-256: `57e564415eba760e9f97d0471618e81a59b36f501d22c87d5a0c2cf9837fe22b`
- 진단용 FS 로그 옵션과 5회 반복 설정은 최종 구성에 남기지 않았다.
- `git diff --check` 및 Python 스크립트 구문 검사 통과.

Kconfig 검사는 새 보드 fragment의 선택·의존성 네 경우를 별도 parser로 확인했다. 전체 tree 정규화는 기존 `apps/examples/tls/client/Kconfig`의 `menu option` 구문을 Kconfiglib가 거부해 완료하지 못했다. 빌드 이미지에는 `kconfig-conf`도 없다. 이 결과를 menuconfig 전체 통과로 보고하지 않으며, 실제 clean build/QEMU 검증은 저장소의 기존 configure.sh 방식으로 완료했다.

## 재현

worktree 루트에서:

```sh
docker run --rm --pull=never --platform linux/arm64 \
  -v "$PWD:/work" -w /work/os tizenrt/tizenrt:2.0.1-arm64-local \
  bash -c 'set -e; make distclean; cd tools; ./configure.sh qemu/build_test; cd ..; make -j4'
python3 tools/qemu-build-test/boot-smoke.py --root . \
  --output tmp/qemu-build-test-stage03/boot-new
python3 tools/qemu-build-test/run-testcases.py --root . \
  --suite kernel_tc --check-storage --timeout 1200 \
  --output tmp/qemu-build-test-stage03/kernel-new
python3 tools/qemu-build-test/run-testcases.py --root . \
  --suite filesystem_tc --check-storage --timeout 300 \
  --output tmp/qemu-build-test-stage03/filesystem-new
python3 tools/qemu-build-test/run-testcases.py --root . \
  --suite drivers_tc --check-storage --timeout 180 \
  --output tmp/qemu-build-test-stage03/drivers-new
python3 tools/qemu-build-test/storage-smoke.py --root . \
  --output tmp/qemu-build-test-stage03/smartfs-new
```

`drivers_tc`는 위 8개 실패가 남아 종료 코드 1이다. `--check-storage`는 새 부팅의 `/mnt`에 보존용 파일을 쓰고, suite 종료 집계 및 후속 셸 검사 뒤 내용을 확인한다. TC 집계와 저장장치 보존 결과를 함께 result.json에 남긴다.

## 증거와 다음 단계

Ignored 로컬 증거: `tmp/qemu-build-test-stage03/`.

- `kernel-expectation/`: 저장장치 추가 전 kernel 432/0
- `filesystem-initial/`, `drivers-initial/`: RAM 장치 연결 직후 결과
- `filesystem-guard-red/`: TC 202/0이지만 `/mnt` 보존 검사 실패한 재현
- `smartfs-smoke/`, `smartfs-fixed/`, `smart-debug*/`, `smart-exhaustion-fixed/`: stress 오류 및 진단 로그. 진단 로그의 정상 ENOSPC 출력도 smoke의 엄격한 오류 검사에 걸리므로 최종 PASS 증거로 사용하지 않는다.
- `final-boot/`, `final-kernel/`, `final-filesystem/`, `final-drivers/`, `final-smartfs/`: 최종 동일 firmware 검증
- `final.elf`, `final.bin`, `final.config`, `build-final.log`, `manifest.json`, 소스 patch 및 새 파일 snapshot

C++와 네트워크는 아직 활성화하지 않았다. 다음 기능 단계에서 각각 추가해 검증한다. CircleCI 변경·실행, 물리 보드 시험 및 원격 push는 하지 않았다.
