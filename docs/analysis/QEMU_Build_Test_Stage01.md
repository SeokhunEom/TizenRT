# QEMU build_test: 0~1단계 검증

검증일: 2026-09-18 KST. 브랜치: `codex/qemu-build-test`.
기준 커밋: `94a61e7e2998ef4c4f77331d13b31986ec97b726`.
Worktree: `/Volumes/T7/Dev/TizenRT/codex/qemu-build-test`.

## 결과와 범위

- 원본 `qemu/build_test`는 GCC 10.3.1 / GNU ld 2.36.1에서 링크 실패했다.
- `build/configs/qemu/Make.defs`의 GCC 전용 옵션 `-nostartfiles -nodefaultlibs`를 제거한 후 clean build가 통과했다. 이 타깃은 GCC 드라이버가 아닌 `arm-none-eabi-ld`를 직접 호출하므로 해당 옵션이 필요 없다. 실제 라이브러리는 링크 명령이 명시한다.
- `build/configs/qemu/build_test/defconfig`는 수정하지 않았다. 생성된 `os/.config`도 원본과 바이트 단위로 같다.
- 패치된 LM3S6965EVB / Cortex-M3 QEMU에서 새 부팅 2회와 `help`, `ps`, `free`가 통과했다.
- 커널/드라이버/SmartFS 테스트 명령 실행, C++/네트워크 활성화 및 CircleCI 변경은 후속 단계이다. 명령 목록에 존재하는 것과 테스트 통과는 구분한다.
- 소스 수정은 공용 QEMU Make.defs 1줄이다. 환경 Dockerfile과 부팅 검사 스크립트를 추가했다.

## 환경

| 항목 | 확인값 |
|---|---|
| Docker 플랫폼 | `linux/arm64` |
| 컴파일러 | GNU Arm Embedded 10.3-2021.10 / GCC 10.3.1 |
| 링커 | GNU ld 2.36.1.20210621 |
| 빌드 이미지 | `tizenrt/tizenrt:2.0.1-arm64-local` |
| 빌드 이미지 ID | `sha256:16b314997d3ed7901fe711ff85e1cd8e90885f44bb3d683fc7040699ebe2b74c` |
| QEMU 이미지 | `tizenrt/qemu-build-test:2.12.0-16m` |
| QEMU 이미지 ID | `sha256:58b19f728ea2a5bf146b2ed1919472aa0f263007f6f13b8d2654a36a0920daa8` |
| QEMU 버전 | 2.12.0 + 저장소의 `qemu-2.12.0-rc1_16m_ram_size.patch` |
| QEMU 메모리 | Flash 128MiB / SRAM 16MiB / SDRAM 512MiB |
| TizenRT 사용 RAM | `0x20000000`부터 16MiB, heap 1개 |

로컬 `DOCKER_DEFAULT_PLATFORM=linux/amd64` 설정이 있으므로 Docker 명령마다 `--platform linux/arm64`를 명시한다. 초기 플랫폼 미지정 시 로컬 ARM64 이미지를 찾지 못하고 pull이 실패했다.
이 빌드 이미지는 로컬 ARM64 환경이며 CircleCI의 외부 환경 변수 `DOCKER_IMG_VERSION`과 동일한 이미지라고 검증한 것은 아니다.

원래 문서의 QEMU 2.12.0-rc1 다운로드 URL은 HTTP 404였다. 공식 2.12.0 정식 배포본을 사용했고 저장소 패치는 fuzz 없이 적용됐다. 호스트 QEMU 11.1.1은 이 실행에 사용하지 않았다.

- 소스: `https://download.qemu.org/qemu-2.12.0.tar.xz`
- 내려받은 소스 SHA-256: `e69301f361ff65bf5dabd8a19196aeaa5613c1b5ae1678f0823bdf50e7d5c6fc`
- 저장소 패치 SHA-256: `33a4b6fd69b45f16cdfd63b94ff8c2aaf1a9824c5e4904f6eed944a914da7c6c`

## 산출물과 실행 증거

| 항목 | 값 |
|---|---:|
| ELF text | 472,455 bytes |
| ELF data | 308 bytes |
| ELF bss | 24,788 bytes |
| tinyara.bin | 472,764 bytes |
| ARM ELF entry | `0x2e1` |
| `free` total | 16,752,112 bytes |
| `free` used | 23,072 bytes |
| `free` free | 16,729,040 bytes |
| `free` largest | 16,722,592 bytes |

ELF는 ARM EABI5 soft-float이며 Flash load segment는 `0x00000000`, data/bss의 실행 주소는 `0x20000000` 영역이다.

- ELF SHA-256: `326afdcaae4b8bd719eab7c80aa1bd81498b6241e02df50d2c52bdea8ab9dc1e`
- BIN SHA-256: `694aec060279c5d074ea8a3f3b1843a472b09a6c5e8008a0ffb84da3edec947c`
- 원본 defconfig SHA-256: `fa309a2dc89df90eb4ec790d58c844bd8f15be4e6ab7ff3c3a6b1d54418d7780`

두 성공 실행 모두 `help`에서 `kernel_tc`, `drivers_tc`, `filesystem_tc`, `smart`, `smart_test` 명령을 확인했고 `ps`의 task 목록과 `free`의 메모리 수치를 확인했다. suite 자체는 실행하지 않았다.

TASH의 첫 프롬프트는 built-in 명령 등록보다 먼저 출력될 수 있다. 최초 검사에서는 `help`에 기본 셸 명령만 보여 실패했다. 검사 스크립트가 `tc_main`의 준비 메시지까지 기다리도록 변경한 후 두 번의 새 부팅이 통과했다. TizenRT 초기화 코드는 변경하지 않았다.

## 재현

다음 명령은 이 worktree 루트에서 실행한다. 빌드 이미지가 위 ID와 일치하는지 먼저 확인한다.

```sh
docker image inspect tizenrt/tizenrt:2.0.1-arm64-local --format '{{.Id}}'

docker run --rm --pull=never --platform linux/arm64 \
  -v "$PWD:/work" -w /work/os \
  tizenrt/tizenrt:2.0.1-arm64-local \
  bash -c 'set -e; make distclean; cd tools; ./configure.sh qemu/build_test; cd ..; make -j4'
```

환경 이미지를 다시 만들 때의 build context:

```sh
mkdir -p tmp/qemu-build-test-env
curl -fL https://download.qemu.org/qemu-2.12.0.tar.xz \
  -o tmp/qemu-build-test-env/qemu-2.12.0.tar.xz
cp build/configs/qemu/qemu-2.12.0-rc1_16m_ram_size.patch \
  tmp/qemu-build-test-env/16m.patch
DOCKER_BUILDKIT=0 docker build --platform linux/arm64 --pull=false \
  -f tools/qemu-build-test/Dockerfile \
  -t tizenrt/qemu-build-test:2.12.0-16m tmp/qemu-build-test-env
```

Dockerfile은 다운로드한 archive의 위 SHA-256을 검사한다. Ubuntu 패키지는 현재 저장소에서 받으므로 재빌드 이미지의 ID까지 동일함을 보장하지는 않는다. 위 이미지 ID가 이번 실행 증거의 기준이다.

```sh
python3 tools/qemu-build-test/boot-smoke.py \
  --root . --output tmp/qemu-build-test-stage01/boot-new
```

스크립트는 읽기 전용 firmware mount와 네트워크가 차단된 컨테이너에서 실행한다. 새 부팅, 명령 등록 준비 메시지, 각 명령의 출력 표식과 다음 프롬프트를 확인한다. assertion/fault, 프로세스 조기 종료, 제한시간 초과는 실패다. 확인이 끝나면 자신의 컨테이너를 종료한다. `serial.log`와 `result.json`을 남긴다.

## 로컬 증거 위치

`tmp/qemu-build-test-stage01/`는 저장소의 기존 `/tmp/` ignore 규칙에 따라 Git에 포함되지 않는다. 이후 기능 추가 시에도 이 기준 산출물을 보존한다.

- `build-original-arm64.log`, `link-repro.log`: 원본 링크 실패와 실제 명령
- `build-link-fix.log`: 최소 수정 뒤 링크 통과
- `build-clean.log`: clean build 및 ELF header/segment 확인
- `boot-01/`: 초기 명령 등록 대기 부족으로 실패한 검사
- `boot-02/`, `boot-03/`: 성공한 두 새 부팅의 serial/result
- `tinyara`, `tinyara.bin`, `tinyara.map`: 기준 펌웨어 사본
- `defconfig.original`, `effective.config`: 설정 사본
- `toolchain-image.json`, `qemu-image.json`, `versions.txt`: 환경 정보
- `qemu-image-build.log`: QEMU 패치/빌드 로그
- `manifest.json`: 결과 요약과 산출물 해시

## 남아 있는 진단 메시지와 후속 단계

- 펌웨어 빌드에 기존 Make jobserver 경고가 있다. clean build 종료 코드는 0이다.
- QEMU 빌드의 dtc 생성 단계에서 flex/bison 없음 메시지가 출력되었으나 빌드 및 설치가 끝났고 실제 emulator 실행도 통과했다.
- baseline은 네트워크를 끄므로 QEMU의 `stellaris_enet.0 has no peer` 경고가 있다.
- 컨테이너가 worktree의 외부 Git 메타데이터를 읽을 수 없어 펌웨어의 Commit Hash는 `NA`이다. 본 문서의 기준 SHA, 소스 diff, 펌웨어 SHA로 실행 대상을 식별한다.
- 다음 작업은 계획의 2단계: 현재 활성화된 테스트의 실제 실행 결과와 QEMU 장치 의존성을 분류하는 것이다.
