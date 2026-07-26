# AI Build Runbook

이 문서는 `/Volumes/T7/Dev/TizenRT/codex/documentation-guides/docs/AI_Build_Runbook.md`를 이 저장소의 Mac mini Apple Silicon 환경에 맞게 옮기고, `tizenrt-2.0.1-arm64` 이미지로 직접 실행해 보완한 버전이다.

## 검증 범위

- 호스트: Apple Silicon Mac mini, Docker Desktop ARM64
- 이미지: `tizenrt/tizenrt:2.0.1-arm64-local`
- 실행 스크립트: Homebrew Bash 5.x의 `os/dbuild.sh menu`
- 문서/QEMU checkout: `/Volumes/T7/Dev/TizenRT/codex/qemu-armv8m-kernel-tc`,
  branch `codex/qemu-armv8m-kernel-tc`
- `dbuild.sh menu` 직접 빌드 checkout: `/Volumes/T7/Dev/TizenRT/main`, HEAD `85056ad92`
- 검증일: 2026-07-24

이 문서는 빌드 재현 절차를 정의한다. 보드의 실제 플래시, UART, 무선, 센서 동작은 별도 하드웨어 검증이 필요하다.

## 0. 기본 실행 방식: `dbuild.sh menu`

이 checkout에서는 개별 `configure.sh`와 `make`를 조합하기보다 `os/`에서 `./dbuild.sh menu`를 실행하고 메뉴 선택으로 보드, config, clean/build를 결정한다.

```bash
export PATH="$(brew --prefix)/bin:$PATH"
cd "$TIZENRT_ROOT/os"
./dbuild.sh menu
```

메뉴에서 보드와 config를 선택한 뒤에는 다음 의미를 구분한다.

| 메뉴 선택 | 실제 작업 |
| --- | --- |
| `1. Build with Current Configuration` | 현재 `.config`로 build |
| `4. Clean Build` | 현재 설정의 `make clean`; 보드/config 재선택 없음 |
| `5. Clean Build and Re-Configure` | `distclean` 후 보드와 config를 다시 선택 |

보드/config를 바꾸면서 완전히 처음부터 빌드하려면 `5`를 선택하고, 다시 보드와 config를 고른 다음 `1`을 선택한다. 단순히 현재 설정의 출력만 지우려면 `4`를 사용한다.

메뉴 번호는 checkout의 config 목록에 따라 달라질 수 있다. 화면에 표시된 번호를 사용하거나 보드/config 이름을 직접 입력한다.

## 1. 안전 경계와 사전 조건

ARM64 Docker 이미지는 신뢰하는 소스 checkout에서만 사용한다. `dbuild.sh`는 소스 디렉터리를 컨테이너에 쓰기 가능하게 bind mount하고 `--privileged`로 빌드한다.

필요한 도구와 조건:

- Docker Desktop이 실행 중이고 `docker info`의 architecture가 `aarch64` 또는 `arm64`
- Homebrew Bash 4 이상
- Python 3
- `tizenrt/tizenrt:2.0.1-arm64-local` 이미지
- 소스 및 빌드 출력에 충분한 디스크 공간
- macOS `/bin/bash` 3.2를 `dbuild.sh`에 사용하지 않음

```bash
export TIZENRT_ROOT="${TIZENRT_ROOT:-/Volumes/T7/Dev/TizenRT/codex/qemu-armv8m-kernel-tc}"
export BASH_BIN="${BASH_BIN:-$(brew --prefix bash)/bin/bash}"
export IMAGE_TAG="tizenrt/tizenrt:2.0.1-arm64-local"

test -d "$TIZENRT_ROOT/os"
test -x "$BASH_BIN"
"$BASH_BIN" --version | head -1
docker info --format 'Docker={{.ServerVersion}} Arch={{.Architecture}} CPUs={{.NCPU}}'
docker image inspect "$IMAGE_TAG" >/dev/null
```

## 2. ARM64 이미지 준비

이미지가 없거나 Dockerfile이 변경된 경우 저장소 루트에서 다시 만든다. Docker credential helper를 읽지 않도록 임시 Docker 설정을 사용한다.

```bash
export TIZENRT_HOST_HOME="$HOME"
export TIZENRT_IMAGE_HOME="$(mktemp -d)"
mkdir -p "$TIZENRT_IMAGE_HOME/.docker"
printf '{}\n' > "$TIZENRT_IMAGE_HOME/.docker/config.json"
export TIZENRT_DOCKER_CONFIG="$TIZENRT_IMAGE_HOME/.docker"
export TIZENRT_DOCKER_HOST="unix://${TIZENRT_HOST_HOME}/.docker/run/docker.sock"

env \
  HOME="$TIZENRT_IMAGE_HOME" \
  DOCKER_CONFIG="$TIZENRT_DOCKER_CONFIG" \
  DOCKER_HOST="$TIZENRT_DOCKER_HOST" \
  docker buildx build \
    --platform linux/arm64 \
    -t "$IMAGE_TAG" \
    --load \
    tools/docker/tizenrt-2.0.1-arm64
```

이미지 확인:

```bash
env \
  HOME="$TIZENRT_IMAGE_HOME" \
  DOCKER_CONFIG="$TIZENRT_DOCKER_CONFIG" \
  DOCKER_HOST="$TIZENRT_DOCKER_HOST" \
  docker run --rm --platform linux/arm64 "$IMAGE_TAG" bash -lc '
    set -eu
    uname -m
    gcc --version | head -1
    python3 --version
    protoc --version
    command -v grpc_cpp_plugin
    file /opt/gcc-arm-none-eabi-10.3-2021.10/bin/arm-none-eabi-gcc
    fiptool --version
  '
```

예상 결과에는 `aarch64`, GCC 10.3.x, Python 3.8.x, protoc 3.5.0, ARM64 compiler executable, ARM64 `fiptool`이 포함된다. 다음 음성 probe는 기본 GNU Arm GCC가 x86-64 바이너리가 아님을 확인한다.

```bash
env \
  HOME="$TIZENRT_IMAGE_HOME" \
  DOCKER_CONFIG="$TIZENRT_DOCKER_CONFIG" \
  DOCKER_HOST="$TIZENRT_DOCKER_HOST" \
  docker run --rm --platform linux/arm64 "$IMAGE_TAG" bash -lc '
    ! file /opt/gcc-arm-none-eabi-10.3-2021.10/bin/arm-none-eabi-gcc | grep -q "x86-64"
  '
```

### Docker 플랫폼 자동 선택

`dbuild.sh`가 `docker info --format '{{.Architecture}}'`를 읽어 Docker platform을 자동 선택한다.

위 이미지 준비/확인 명령은 Docker를 직접 호출하므로 `--platform`만 명시한다. `dbuild.sh menu`를 사용할 때는 아래 자동 선택에 맡기며 `DOCKER_DEFAULT_PLATFORM`을 설정하지 않는다.

| Docker architecture | 선택 platform | 기본 이미지 |
| --- | --- | --- |
| `aarch64`, `arm64` | `linux/arm64` | `tizenrt/tizenrt:2.0.1-arm64-local` |
| `amd64`, `x86_64` | `linux/amd64` | config/public image |

선택된 platform은 모든 `docker run`에 `--platform`으로 전달된다. 따라서 셸에 잘못된 `DOCKER_DEFAULT_PLATFORM=linux/amd64`가 남아 있어도 ARM64 Mac의 local image를 쓰기 위해 별도 환경변수를 지정할 필요가 없다. 실행 시 다음 줄로 선택 결과를 확인할 수 있다.

```text
Docker Image : tizenrt/tizenrt:2.0.1-arm64-local
Docker Platform : linux/arm64
```

## 3. 메뉴 기반 빌드 절차

### 최초 보드/config 선택

```bash
cd "$TIZENRT_ROOT/os"
./dbuild.sh menu
```

화면에서 `Select Board`와 `Select Configuration`을 순서대로 고른다. 설정 파일이 생성되면 `Select build Option`이 표시된다.

### config를 바꾸면서 distclean

```text
Select build Option
1. Build with Current Configuration
2. Re-configure
3. Modify Current Configuration
4. Clean Build
5. Clean Build and Re-Configure
6. Build SmartFS Image
t. Build Test
x. Exit
```

`5`를 입력하면 `distclean`이 실행되고 다시 보드/config 선택 화면으로 돌아간다. 새 보드와 config를 선택한 뒤 `1`을 입력해 빌드한다.

### 현재 config 재빌드

같은 board/config를 그대로 다시 빌드하려면 `./dbuild.sh menu`에서 `1`을 선택한다. 출력만 지우고 다시 빌드하려면 `4`를 선택한 다음 메뉴에서 `1`을 선택한다. config를 다시 고르려면 `5`를 사용한다.

## 4. 대표 보드 빌드 예

다음은 메뉴 입력의 개념 예시다. 실제 번호는 실행 시 표시된 메뉴를 따른다.

```bash
cd "$TIZENRT_ROOT/os"
./dbuild.sh menu
# 5: Clean Build and Re-Configure
# <board>: rtl8721csm
# <config>: hello
# 1: Build with Current Configuration
```

빌드 결과는 저장소 루트에서 확인한다.

```bash
test -s "$TIZENRT_ROOT/build/output/bin/kernel_rtl8721csm_200204.trpk"
```

검증 당시 결과:

| recipe | artifact | 결과 |
| --- | --- | --- |
| `rtl8721csm/hello` | `build/output/bin/kernel_rtl8721csm_200204.trpk` | `main` checkout에서 `menu → 5 → rtl8721csm → hello → 1`로 성공, 1,157,440 bytes; partition/size/header verification 성공 |
| `rtl8730e/flat_dev_ddr` | `build/output/bin/fip.bin` | 성공, 293,057 bytes; partition/size/header verification 성공 |
| `bk7239n/hello` | `build/output/bin/kernel_bk7239n_200204.trpk` | 이 checkout에서 검증 실패; 아래 제한 사항 참고 |

### `bk7239n/hello`의 현재 제한 사항

`bk7239n/hello`를 `distclean` 후 첫 빌드하면 TF-M prehandle이 만드는 `_otp.h`가 dependency scan보다 늦게 준비되어 다음 오류가 날 수 있다.

```text
fatal error: _otp.h: No such file or directory
No rule to make target '/root/tizenrt/os/include/stdarg.h'
```

이 checkout에서 오류 후 메뉴의 `5. Clean Build and Re-Configure`를 다시 실행하지 않고 `1. Build with Current Configuration`을 한 번 더 선택해도 `os/include/stdarg.h` 오류가 남아 artifact가 생성되지 않았다. 따라서 이 recipe는 현재 Mac ARM64 runbook의 성공 recipe로 표시하지 않는다. 원인을 해결하려면 `build/configs/bk7239n/hello/defconfig`의 `CONFIG_ARCH_STDARG_H` 조건과 TF-M prebuild 의존성 순서를 별도 소스 변경으로 다뤄야 하며, 이 문서 작성 범위에서는 소스를 수정하지 않았다.

## 5. 빌드 결과와 Git 상태 확인

artifact만 있으면 성공으로 간주하지 않는다. semantic build error, tracked diff, 예상하지 않은 untracked 파일을 함께 확인한다.

```bash
cd "$TIZENRT_ROOT"

test -z "$(git diff --name-only)$(git diff --cached --name-only)"

unexpected_status="$({
  git status --porcelain=v1 --untracked-files=all |
    awk '$1 == "??" && $2 ~ /(^|\/)\._[^\/]*$/ { next } { print }'
})"
test -z "$unexpected_status"

if rg -n "fatal error:|No rule to make target|Error [0-9]+|FAILED|verification FAILED" \
  os/build.log 2>/dev/null; then
  echo "semantic build error found in os/build.log" >&2
  exit 1
fi
```

AppleDouble 파일(`._*`)은 `/Volumes/T7`의 파일 시스템 메타데이터로 생성될 수 있다. 이 파일을 tracked source 변경으로 보고하지 말고, 정확한 경로를 확인한 뒤에만 별도로 정리한다. 빌드가 성공했다는 이유로 broad cleanup을 실행하지 않는다.

## 6. 장애 대응 순서

1. `docker info`와 `docker image inspect`로 Docker Desktop과 이미지를 확인한다.
2. `dbuild.sh` 출력의 `Docker Platform`이 호스트와 이미지에 맞는지 확인한다.
3. `os/.config`가 원하는 board/config인지 확인한다.
4. config를 바꾸려면 메뉴의 `5. Clean Build and Re-Configure`를 사용한다.
5. 오류의 첫 번째 compiler/linker 오류를 로그에서 찾는다. 마지막 `make` 오류만 원인으로 사용하지 않는다.
6. artifact, verification summary, Git 상태를 함께 보관한다.

`docker login` 또는 macOS Keychain을 수정하는 작업은 이 runbook의 기본 단계가 아니다. 이미지가 로컬에 없을 때는 먼저 image build와 probe를 수행한다.

## 7. QEMU 후속 검증

QEMU는 `hello`, `loadable_all`, `loadable_apps`, `xip_all` 네 config를
지원한다. 사람이 직접 TASH를 확인할 때는 [Mac 터미널 가이드](../Human/QEMU_ARMv8M_Terminal_Guide.md)의
`hello` 절차를 사용하고, loadable/XIP는 [Mac QEMU ARMv8-M 가이드](Mac_QEMU_ARMv8M_TASH_KernelTC.md)의
runner를 사용한다.

```bash
cd "$TIZENRT_ROOT"
python3 .github/scripts/qemu-armv8m-kernel-tc.py \
  --config loadable_all \
  --state-image build/qemu-armv8m/qemu.state \
  --max-reboots 1 \
  --timeout 1200
```

QEMU의 메모리 모델은 `hello`에서 main RAM 12 MiB와 SSRAM heap 512 KiB,
loadable/XIP에서 main RAM 4 MiB + loaded RAM 8 MiB + SSRAM heap 512 KiB를
사용한다. runner의 persistent state는 RAM-backed flash, A/B kernel/user
slot, boot parameter를 보존한다.

이번 검증에서 `hello`는 build/TASH/Kernel TC 실행까지 확인했지만
`PASS : 457, FAIL : 2`였다. `loadable_all`은 binary-manager가 common/app1/app2를
탐색하고 TASH를 띄웠으며 A/B state staging/bootparam 단위 검증을 통과했지만
semaphore assertion 뒤 timeout이었다. `loadable_apps`도 common/app1/app2를
탐색하고 TASH를 띄웠지만 같은 semaphore holder assertion 뒤 timeout이었다.
`xip_all`은 common/app1 XIP 부팅을 확인했지만 scheduler testcase 뒤 timeout이었다.
따라서 이 결과는 QEMU boot/package smoke 증거이지,
모든 config의 `FAIL : 0` 또는 실제 보드 동작을 의미하지 않는다.
Binary Manager의 손상/누락 package negative 경로는
`binary_manager_load: Invalid Header data, name : common/app1` 진단과
alternate-slot 성공 진단의 부재, recovery reboot 종료를 기준으로
검증한다. common은 `binary_manager_load: common Header Checking Success`,
app1은 `binary_manager_load: app1 Header Checking Success`를 금지한다.
