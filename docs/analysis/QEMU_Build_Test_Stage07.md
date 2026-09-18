# QEMU build_test: 7단계 GitHub Actions 연결

작업일: 2026-09-18 KST. 브랜치: `codex/qemu-build-test`.
기준은 6단계 커밋 `1720fbced4ad7da8c2449194addb17cc5dbb8bea`다.
6단계에서 예정했던 CircleCI 연결을 사용자 요청에 따라 GitHub Actions로 변경했다.

## 실행 범위

[workflow](../../.github/workflows/qemu-build-test.yml)는 모든 브랜치의 `push`,
`pull_request`, 수동 `workflow_dispatch`에서 실행한다. fork의 Actions 기능을 활성화하면
upstream 계정이나 별도 registry secret 없이 fork 안에서 실행할 수 있다.
기존 CircleCI 설정과 `qemu/build_test` defconfig는 변경하지 않는다.

표준 `ubuntu-24.04-arm` GitHub-hosted runner에서 다음을 실행한다.

1. 기존 full-set 판정기와 새 CI 요약·빌드 실패 전파 테스트.
2. ARM64 CI 이미지 생성, `make distclean`, `qemu/build_test` configure 및 build.
3. 같은 이미지의 immutable ID로 QEMU 부팅 한 번, kernel 포함 전체 세 회차 실행.
4. 빌드·실행의 commit, ELF hash, effective config hash 일치 검사 및 Job Summary 작성.
5. 성공·실패 모두 firmware와 로그를 artifact로 업로드하고 7일 보관.

각 회차는 network peer, network TC, C++ smoke, libc++ UTC, filesystem TC,
SmartFS fill/delete 100회, drivers TC, kernel TC를 포함한다. 기존 full-set 판정기가
매 workload 뒤 task 복귀와 저장장치를 확인하고, 2→3회차 heap 및 파일시스템 안정성을
검사한다. CI 요약기는 이 판정을 재사용하며 부분 실행을 성공으로 표시하지 않는다.

정상 최종 상태는 `pass_with_known_driver_failures`다. PWM/watchdog/ADC 미지원에 따른
기존 driver TC 14 PASS / 8 FAIL을 숨기지 않는다. 정확한 기존 signature와 장치 부재를
만족하는 경우만 허용하며, 새 실패·종료 누락·timeout·자원 증가·불완전한 결과는 CI 실패다.

동일 이벤트·브랜치/PR의 이전 실행은 새 실행으로 취소된다. push와 PR 이벤트는 별도이므로
같은 commit에 두 실행이 생길 수 있다. 전체 job 제한은 190분, 이미지/빌드 40분,
QEMU full-set 140분이다. full-set 내부 watchdog의 최대 137분 뒤에도 정리·요약 시간을 확보한다. `contents: read`만 부여하고 checkout credential을 남기지 않는다.
QEMU 컨테이너는 호출자의 UID/GID로 실행하여 Linux에서도 결과 파일을 갱신할 수 있게 한다.
빌드 컨테이너는 기존 root 실행 방식을 사용하며, host가 읽을 수 있는 ELF·BIN·config를
별도 evidence 디렉터리로 복사한다. 두 컨테이너는 외부 네트워크를 차단하며 QEMU peer는 같은 컨테이너 내부에 있다.
이미지 생성 단계는 공개 패키지·소스 다운로드 때문에 네트워크를 사용한다.

## CI 환경

[Dockerfile.ci](../../tools/qemu-build-test/Dockerfile.ci)는 로컬 전용
`tizenrt/tizenrt:2.0.1-arm64-local` 이미지에 의존하지 않는 별도 이미지다.
기존 검증 환경과 GCC/QEMU 및 Ubuntu userspace를 맞췄다.

- Ubuntu 16.04 ARM64 base는 image digest로 고정한다.
- GNU Arm Embedded GCC 10.3-2021.10 ARM64 archive와 QEMU 2.12.0 source는 SHA-256을 검사한다.
- 검증기는 Ubuntu에 포함된 Python 3.5.2를 사용한다. 별도의 Python source build는 후속 P3 정리에서 제거했다.
- 저장소의 `qemu-2.12.0-rc1_16m_ram_size.patch`를 fuzz 없이 적용하고 ARM softmmu만 빌드한다.
- checkout 및 upload-artifact action은 v7.0.1의 commit SHA로 고정한다.
- apt 패키지는 배포판 archive를 사용하므로 이미지 전체의 bit-for-bit 재현성을 보장하지 않는다.

오래된 QEMU와 기존 빌드 호환성을 유지하는 전용 테스트 환경이다. host는 현대 Ubuntu
runner이며, 이 이미지를 제품 배포 환경으로 사용하는 변경은 포함하지 않는다.
초기 버전은 매 job에서 이미지를 만들고 별도 image registry나 cache를 운영하지 않는다.

## 로컬에서 같은 경로 실행

native ARM64 Linux Docker daemon, Bash, Python 3가 필요하다. macOS ARM64의 Docker Desktop도
사용할 수 있다. 빌드가 worktree의 생성물을 변경하므로 동일 worktree에서 다른 빌드를
동시에 실행하지 않는다. 출력 디렉터리는 새 경로나 빈 디렉터리여야 한다.

```sh
python3 tools/qemu-build-test/test-full-set.py -v
python3 tools/qemu-build-test/test-ci-summary.py -v
python3 tools/qemu-build-test/test-ci-build.py -v

evidence=tmp/qemu-build-test-ci
bash tools/qemu-build-test/ci-build.sh "$evidence"
python3 tools/qemu-build-test/full-set.py --root . \
  --image "$(cat "$evidence/image-id.txt")" \
  --rounds 3 --output "$evidence/full-set"
python3 tools/qemu-build-test/ci-summary.py --output "$evidence"
```

빌드나 full-set이 실패해도 마지막 요약 명령을 실행하면 실패 요약이 남고 exit 1을 반환한다.
Actions에서는 `if: always()`로 요약과 artifact 업로드를 시도한다. runner 강제 종료나
서비스 장애까지 artifact 보존을 보장하는 것은 아니다.

artifact 이름은 `qemu-build-test-<run_id>-<run_attempt>`이며 다음 파일을 포함한다.

- `image-build.log`, `versions.log`, `build.log`, `image-id.txt`, `build.json`
- `firmware/tinyara`, `firmware/tinyara.bin`, `effective.config`
- `full-set/result.json`, `full-set/serial.log`, `full-set/runner.log`
- `ci-summary.json`, `summary.md`

실패 시에는 그 시점까지 생성한 파일을 보관한다. `effective.config`는 숨김 파일을
기본 제외하는 artifact 업로드에서도 빌드 설정이 남도록 명시적으로 복사한 파일이다.

## fork에서 활성화할 때

이번 작업은 로컬 구현과 검증까지만 수행한다. commit, push, 원격 설정 변경과
GitHub workflow 실행은 하지 않는다.

나중에 fork로 push하면 Actions 활성화 상태에서 branch push 이벤트로 실행된다.
fork에서 workflow가 비활성 상태라면 저장소의 Actions 탭에서 활성화한다.
수동 Run workflow 버튼은 workflow 파일이 기본 브랜치에도 있어야 제공되며,
그 후 실행할 브랜치를 선택할 수 있다. 기본 브랜치 반영 전에는 branch push 트리거를 쓴다.
외부 기여자의 fork PR은 GitHub 설정에 따라 실행 승인이 필요할 수 있다.

표준 GitHub-hosted runner의 public repository 실행은 무료지만, larger runner는 유료이고
artifact/cache 저장 및 동시 실행에는 별도 제한이 있다. 이번 설정은 표준 runner와
7일 artifact 보관을 사용한다.

## 로컬 검증 증거

실행 증거 경로: `tmp/qemu-build-test-stage07/` (git 제외).

- `ci-01/`: 새 이미지 clean build는 성공했지만 Python 3.5가 기존 SmartFS 검증기의
  scoped regex flag를 지원하지 않아 1회차에서 중단됐다. 실제 실패 결과로 CI 요약도
  `fail`, exit 1을 반환함을 확인했다. Python 3.8.18을 고정 설치해 해결했다.
- `ci-02/`: 수정한 Python 이미지 생성 및 clean build exit 0, `versions.log`에서 3.8.18 확인.
- `linux-ownership.json`: macOS bind mount 매핑을 거치지 않는 Linux Docker volume에서
  root:root 0644 결과 파일을 UID 1001로 갱신하면 PermissionError/exit 1임을 재현했다.
  inner와 host 역할을 같은 UID/GID 1001로 실행하면 생성·갱신 모두 exit 0이다.
  runtime에 host UID/GID를 적용했다. 임시 volume은 검증 후 삭제했다.
- `fresh-checkout.log`: 설정과 빌드 생성물이 없는 임시 local clone에서 비root로
  `make distclean` 및 `configure.sh qemu/build_test`를 실행하고 defconfig 일치 확인, exit 0.
- `ci-03/`: builder까지 비root로 바꾸면 macOS 공유 디렉터리에서 `install`의 chmod가
  `Operation not permitted`로 실패했다. builder는 기존 root 실행을 유지하고,
  host가 container의 결과를 갱신하는 runtime에만 UID/GID를 적용했다.
- `ci-04/`: 최종 clean build와 full-set 모두 exit 0. 부팅 한 번, 전체 세 회차를
  2,271.555초(약 37분 52초) 실행했고 최종 상태는 `pass_with_known_driver_failures`다.
  workflow와 같은 CI 요약 명령도 exit 0이며 `job-summary.md`와 `ci-summary.json`을 확인했다.

각 회차의 TC 수치는 PASS / FAIL이다.

| 회차 | Network | libc++ | Filesystem | Kernel | Drivers | Heap used | Tasks |
|---|---:|---:|---:|---:|---:|---:|---:|
| 1 | 163 / 0 | 795 / 0 | 203 / 0 | 433 / 0 | 14 / 8 | 77,872 | 8 |
| 2 | 163 / 0 | 795 / 0 | 203 / 0 | 433 / 0 | 14 / 8 | 77,872 | 8 |
| 3 | 163 / 0 | 795 / 0 | 203 / 0 | 433 / 0 | 14 / 8 | 77,856 | 8 |

매 회 network peer 7 checks, C++ smoke와 SmartFS fill/delete 100회도 통과했다.
2→3회차 heap은 16바이트 감소했고, 파일명·크기·volume 용량이 동일했다.
`/mnt` available blocks는 1,011→1,010→1,010, largest free block은 세 회 모두
14,321,872바이트다. 모든 workload 뒤 기존 PID 집합 `[0, 1, 2, 3, 4, 5, 7, 8]`을 유지했다.
이는 관측 구간의 안정성 판정이며 모든 잠재적 누수·단편화의 부재를 증명하지는 않는다.

- CI image ID: `sha256:69db5b115af626dd2281d0d25644b8394d0508782fe1aaac703c69b2c03121ac`
- ELF SHA-256: `296c5fb8a836306d8933a2093214a25019e70dbdea669d60384675badd4c4235`
- BIN SHA-256: `0a03752e8065eb372ef06e34a4fdf499419f28ff65e0ee97c79604193c7bf930`
- effective config / defconfig SHA-256: `196d7ada3d78c08a3782ed239786654e372d16d06d03d99fd3dd74306d2db0e1`
- ELF text/data/bss: 2,443,126 / 1,512 / 2,372,900 bytes.

최종 소스 snapshot과 artifact 파일 hash는 실행 후 다시 일치함을 확인했다.


판정기 단위 테스트 19개(기존 full-set 11개, CI summary 7개, 빌드 실패 전파 1개/2개 실패 지점),
Bash 구문, ShellCheck 0.11.0, actionlint 1.7.12, Python AST 및 tracked/untracked whitespace
검사를 통과했다. Standards 리뷰는 지적 0건, Spec 리뷰의 Linux 소유권 P1 1건은 수정 후
재검토에서 닫혔으며 남은 지적은 0건이다.

최종 clean build의 기존 C++ C 전용 옵션 진단 2건과 libcxx-test Makefile의
`Command not found` 10건은 6단계와 종류·횟수가 같다. make exit 0이며, 진단이 전혀 없는
빌드라는 뜻은 아니다. 이 단계에서는 firmware 기능이나 TC의 기준을 변경하지 않았다.

최종 소스·설정 hash와 입력 복사본은 `source-manifest.json`, `final-source/`에 보관했다.
최초 checkout 검증용 임시 clone은 소유권과 기준 commit·clean 상태를 확인한 후 삭제했다.

GitHub-hosted runner의 실제 이벤트 실행·Job Summary 표시·artifact 업로드는 push 전이므로
미검증이다. 로컬 Docker의 workflow 동일 명령 검증과 구분한다.

## 참고

- [GitHub-hosted runner 및 ARM64 표준 runner](https://docs.github.com/en/actions/reference/runners/github-hosted-runners)
- [Workflow 문법, 권한 및 timeout](https://docs.github.com/en/actions/reference/workflows-and-actions/workflow-syntax)
- [수동 workflow 실행 조건](https://docs.github.com/en/actions/how-tos/manage-workflow-runs/manually-run-a-workflow)
- [GitHub Actions 요금](https://docs.github.com/en/billing/concepts/product-billing/github-actions)
- [actions/checkout v7.0.1](https://github.com/actions/checkout/releases/tag/v7.0.1)
- [actions/upload-artifact v7.0.1](https://github.com/actions/upload-artifact/releases/tag/v7.0.1)

## 후속 Standards P3 정리

기준 커밋 `9f0e62151` 이후 Standards 리뷰의 두 P3만 정리했다.

- SmartFS 오류 검출의 scoped regex를 `re.I | re.M`과 일반 행 anchor로 표현하고,
  Python 3.8 다운로드·컴파일·별도 PATH 설정을 제거했다. LF/CRLF의 단독 `Fail` 행을 모두 검사한다.
- `qemu_common.py`로 기존 QEMU session, Docker 실행·metadata·cleanup, C++와 SmartFS
  판정을 옮겼다. boot/cpp/storage/testcase/network/full-set CLI가 이를 공유한다.
  단독 검사 옵션(`--basic-only`, `--suite`, `--check-storage`, `--timeout`)과 성공 기준을 유지한다.
  단독 driver TC는 여전히 raw FAIL / exit 1이며 full-set만 검토된 driver baseline을 허용한다.
- 모든 실행기의 결과에 `docker_command`, `qemu_command`, `container_exit`와 firmware/config
  hash를 공통으로 남긴다. 새 실행이 기존 evidence 디렉터리를 덮어쓰지 않도록 통일했다.
- workflow의 이벤트, 브랜치 필터, concurrency와 3회 검증은 그대로다.
  검증 단계에 공통 helper의 실패 경계 테스트만 추가했다.

새 증거는 `tmp/qemu-build-test-p3/`에 보관한다. 앞의 `stage07/ci-04` 결과는
Python 3.8을 사용했던 이전 구현의 기록이며, 변경 후 결과와 구분한다.
변경 후 로컬 Docker 검증 결과는 다음과 같다.

- 새 이미지의 Python 3.5.2에서 clean build와 boot smoke를 통과했다.
- host 회귀 테스트 29개(full-set 11, 공통 helper 10, CI 요약 7, build 실패 전파 1)를 통과했다.
  컨테이너 안에서도 full-set·공통 helper·CI 요약 28개를 Python 3.5.2로 통과했다.
- 단독 C++ 기본/전체, SmartFS, network TC와 storage guard 검사를 통과했다.
  단독 driver TC는 14 PASS / 8 FAIL 및 exit 1을 유지했고, 손상된 network 응답도 exit 1로 검출했다.
  종료 직전 수집한 FAIL 로그가 최종 JSON 진단에도 포함되는 회귀 검사를 추가했다.
- `ci-01/full-set/result.json`은 1회 부팅, 커널 포함 3회 반복 후
  `pass_with_known_driver_failures`, container exit 0이며 전체 시간은 2,326.140초다.
  각 회차 network 163/0, libc++ 795/0, filesystem 203/0, kernel 433/0,
  drivers 14/8과 C++·network peer·SmartFS 100회 반복을 확인했다.
- 태스크는 매회 8개로 복귀했고 사용 heap은 77,872 → 77,872 → 77,856 bytes였다.
  storage guard와 2→3회차 파일 목록·사용 용량도 유지됐다.
- `ci-summary.py`도 exit 0으로 같은 판정을 생성했다. build metadata와 runtime의
  ELF/config hash가 일치하며, `source-manifest.json`과 `final-source/`에 미커밋 검증 소스를 보관했다.
- Standards와 Spec 재검토에서 이번 변경의 미해결 지적은 없다.
  workflow 이벤트 정책은 수정하지 않았다. 이후 GitHub 실행에서 확인한 시간 제한 문제는 아래에 기록한다.


## GitHub runner의 SmartFS 시간 제한 보정

실제 GitHub 실행 [35340329019](https://github.com/SeokhunEom/TizenRT/actions/runs/35340329019)
(`9f0e62151`)와 [35349469821](https://github.com/SeokhunEom/TizenRT/actions/runs/35349469821)
(`6b41da00f`)는 빌드·network·C++·libc++·filesystem을 통과한 뒤, 1회차 `smart`의
95번째 FILLING에서 `Missing serial marker: b'TASH>>'`로 실패했다.
진행 출력은 계속 있었으나 기본 90초의 절대 대기 시간이 끝나 Python이 QEMU를 종료했다.
전체 workflow 제한이나 SmartFS 오류 판정에 걸린 것이 아니다.

실패 CI의 실제 firmware로 CPU quota 0.65 조건을 맞춰 비교했을 때, `smart` 제한 90초는
90.071초에 같은 오류로 실패했고 300초는 190.687초에 100회 완료 및 sentinel 보존을 통과했다.
이 결과는 제한 CPU의 로컬 재현이며 GitHub 전체 실행 성공을 의미하지 않는다.

- `shell`과 `storage_command`의 선택적 완료 timeout을 전달해 full-set의 `smart`만
  300초를 사용한다. 단독 storage smoke의 기존 기본값과 같다.
- 다른 명령과 명령 echo 대기는 기존 제한을 유지한다. 출력이 계속돼도 제한은 연장하지 않는다.
  정확한 100회 fill/delete, 완료 마커, 오류 검출 및 driver baseline도 유지한다.
- 실제 `full.storage → storage_command → shell → wait` 경로를 사용한 회귀 검사에서
  95초 진행 출력 후 190초 완료는 통과하고, 301초에도 미완료면 timeout을 유지한다.
  190초 완료 사례는 수정 전 동일한 timeout 오류로 실패함을 먼저 확인했다.
- host 테스트 31개, Python 3.5.2 컨테이너의 판정기·공통 helper·CI 요약 테스트 30개를 통과했다.
  Standards·Spec 재검토의 미해결 지적은 없다.
- 수정한 소스를 그대로 사용한 제한 CPU 검증에서도 `smart`는 184.089초에 100회 완료했다.
  sentinel이 보존됐으며 `smart`의 제한 300초, 다른 명령의 제한 90초를 실행 기록으로 확인했다.
  증거는 `tmp/qemu-build-test-timeout/`에 보관한다. GitHub의 전체 3회 실행은 별도 최종 게이트다.
