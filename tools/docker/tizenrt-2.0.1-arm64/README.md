# TizenRT 2.0.1 ARM64 Docker Image

This runbook builds and uses `tizenrt/tizenrt:2.0.1-arm64-local` locally on Docker Desktop for Apple Silicon. It does not publish the image.

The image builds the official Trusted Firmware-A v2.3 `fiptool` at commit `8ff55a9e14a23d7c7f89f52465bcc6307850aa33` and sets `FIPTOOL=/usr/local/bin/fiptool`. This overrides the repository's x86-64 vendored tool for ARM64 `rtl8730e` builds; upstream images leave `FIPTOOL` unset and continue using the vendored binary.

The closed-source `rtl8730e` `elf2bin` binary is the only x86-64 host-tool exception. Because it cannot be rebuilt for ARM64, the image copies only `ld-2.23.so`, `libc-2.23.so`, and `libm-2.23.so` from the pinned Ubuntu Xenial amd64 image and provides the loader/library symlinks expected by the unchanged caller. All compilers and rebuildable host tools remain native ARM64; this image must not be described as fully native execution.

Docker Desktop amd64 binfmt/Rosetta support is required for the pinned source-stage libc version check and for this one `elf2bin` binary. It is not used by the ARM64 toolchains or rebuilt host tools. In the compatibility prototype, only a 4096-byte manifest operation was measured: the ARM64 image produced the byte-identical SHA-256 `e1ea78944e9640733530acd57dd3ff79786e8b2ed37e8751b567fe5215867c87` in 0.27 seconds, compared with 0.26 seconds in the upstream amd64 image. These numbers do not claim whole-build performance parity.

Run each command block from the TizenRT worktree root. The board block changes into `os` exactly once.

## Security Boundary

This is a local compatibility image for reviewed, trusted TizenRT source on Apple Silicon Docker Desktop. The inherited `dbuild.sh` runs as root with `--privileged` and a writable source bind mount; unsupported Xenial is an explicit compatibility exception, not a production-security baseline.

Do not use this workflow for untrusted pull requests, shared CI, native Linux, or worktrees containing credentials. Use a disposable worktree at a reviewed commit, a temporary credential-free Docker config, no extra host mounts, sockets, or forwarded agents, and Docker Desktop Enhanced Container Isolation when available. After every build, inspect source status and remove leftover containers.

## Prerequisites

`dbuild.sh` requires Bash 4 or newer. macOS `/bin/bash` 3.2 is unsupported.

```bash
brew install bash
BASH_BIN="$(brew --prefix bash)/bin/bash"
BASH_MAJOR="$("$BASH_BIN" -c 'printf "%s\n" "${BASH_VERSINFO[0]}"')"
if [ "$BASH_MAJOR" -lt 4 ]; then
  echo "Bash 4 or newer is required: $BASH_BIN" >&2
  exit 1
fi
"$BASH_BIN" --version | head -1
```

## Build

The isolated `HOME` and `DOCKER_CONFIG` avoid the noninteractive macOS Keychain credential helper without reading or overwriting `~/.docker/config.json`. The trap and final removal clean the temporary directory while preserving a failed build's exit status.

```bash
set -e

REAL_HOME="$HOME"
ISOLATED_HOME="$(mktemp -d)"
trap 'exit_status=$?; rm -rf "$ISOLATED_HOME"; exit "$exit_status"' EXIT
mkdir -p "$ISOLATED_HOME/.docker/cli-plugins"
ln -s /Applications/Docker.app/Contents/Resources/cli-plugins/docker-buildx \
  "$ISOLATED_HOME/.docker/cli-plugins/docker-buildx"

HOME="$ISOLATED_HOME" \
DOCKER_CONFIG="$ISOLATED_HOME/.docker" \
DOCKER_HOST="unix://$REAL_HOME/.docker/run/docker.sock" \
docker buildx build --platform linux/arm64 \
  -t tizenrt/tizenrt:2.0.1-arm64-local --load \
  tools/docker/tizenrt-2.0.1-arm64

IMAGE_TAG=tizenrt/tizenrt:2.0.1-arm64-local
IMAGE_ID="$(HOME="$ISOLATED_HOME" \
  DOCKER_CONFIG="$ISOLATED_HOME/.docker" \
  DOCKER_HOST="unix://$REAL_HOME/.docker/run/docker.sock" \
  docker image inspect --format '{{.Id}}' "$IMAGE_TAG")"
test -n "$IMAGE_ID"
printf 'IMAGE_ID=%s\n' "$IMAGE_ID"

rm -rf "$ISOLATED_HOME"
trap - EXIT
```

## Automatic dbuild Selection

When the Docker daemon reports `arm64` or `aarch64`, `os/dbuild.sh` uses `tizenrt/tizenrt:2.0.1-arm64-local` for build, build-test, clean, menuconfig, and download operations. This takes precedence over `CONFIG_DOCKER_VERSION` in a generated `.config`, so every supported board configuration uses the same reviewed local image. If the tag is missing, `dbuild.sh` stops and points back to the Build section instead of pulling or silently falling back to an amd64 image.

The top-level make keeps TizenRT's dependency orchestration order, while recursive compilation uses one job per Docker CPU. Nested application and external-library makes share the parent GNU Make jobserver instead of falling back to `-j1` or starting independent worker pools. The job count priority is:

1. `TIZENRT_BUILD_JOBS`
2. `CONFIG_BUILD_PARALLEL_JOBS` when it is greater than zero
3. the CPU count reported by Docker

An exact image reference can be selected explicitly when needed:

```bash
TIZENRT_DOCKER_IMAGE=registry.example/tizenrt/toolchain:tag \
  ./dbuild.sh build
```

## Probe

```bash
test "$(docker image inspect --format '{{.Id}}' "$IMAGE_TAG")" = "$IMAGE_ID"
docker run --rm --platform linux/arm64 \
  "$IMAGE_TAG" bash -lc '
set -e
test "$(uname -m)" = aarch64
arm-none-eabi-gcc --version | head -1 | grep "10.3"
python3 --version | grep "3.8.18"
protoc --version | grep "3.5.0"
python3 -c "import openpyxl; assert openpyxl.__version__ == \"3.1.5\""
command -v grpc_cpp_plugin
test "$FIPTOOL" = /usr/local/bin/fiptool
file "$FIPTOOL" | grep "ARM aarch64"
"$FIPTOOL" version | grep "v2.3"
test -x /opt/gcc-arm-none-eabi-10.3-2021.10/bin/arm-none-eabi-gcc
'
```

The following negative x86-64 probe must print no match and exit `1`:

```bash
test "$(docker image inspect --format '{{.Id}}' "$IMAGE_TAG")" = "$IMAGE_ID"
docker run --rm --platform linux/arm64 \
  "$IMAGE_TAG" bash -lc '
file "$(command -v arm-none-eabi-gcc)" | grep "x86-64"
'
```

## Board Builds

Start this block from the TizenRT worktree root in a disposable worktree. `dbuild.sh` selects the local ARM64 image automatically; existing board defconfigs remain unchanged.

```bash
set -euo pipefail

: "${IMAGE_TAG:=tizenrt/tizenrt:2.0.1-arm64-local}"
: "${IMAGE_ID:?Run the Build block in this shell first}"

SOURCE_COMMIT="$(git rev-parse HEAD)"
SOURCE_STATUS="$(git status --porcelain=v1 --untracked-files=all)"
if [ -n "$SOURCE_STATUS" ]; then
  echo "Use a clean disposable worktree at the reviewed commit" >&2
  exit 1
fi

BASH_BIN="${BASH_BIN:-$(brew --prefix bash)/bin/bash}"
BASH_MAJOR="$("$BASH_BIN" -c 'printf "%s\n" "${BASH_VERSINFO[0]}"')"
if [ "$BASH_MAJOR" -lt 4 ]; then
  echo "Bash 4 or newer is required: $BASH_BIN" >&2
  exit 1
fi

REAL_HOME="$HOME"
ISOLATED_HOME="$(mktemp -d)"
trap 'exit_status=$?; rm -rf "$ISOLATED_HOME"; exit "$exit_status"' EXIT
mkdir -p "$ISOLATED_HOME/.docker"
printf '{}\n' > "$ISOLATED_HOME/.docker/config.json"
export HOME="$ISOLATED_HOME"
export DOCKER_CONFIG="$ISOLATED_HOME/.docker"
export DOCKER_HOST="unix://$REAL_HOME/.docker/run/docker.sock"

cd os

FAIL_PATTERN='bad substitution|qemu.*(Could not open|No such file)|No fip\.bin|manifest.*(No such file|not found|missing|cannot stat)|(No such file|not found|missing|cannot stat).*manifest|recipe for target .* failed|(^|[[:space:]:])Killed([[:space:]]|$)|Out of memory|OOM|oom-kill|\.depend.*(Error|failed)|Error.*\.depend'

build_board() {
  local board="$1" config="$2" artifact="$3"
  local log="../build-${board}-${config}.log"

  if [ "$(docker image inspect --format '{{.Id}}' "$IMAGE_TAG")" != "$IMAGE_ID" ]; then
    echo "Local image tag changed after the reviewed build: $IMAGE_TAG" >&2
    return 1
  fi

  rm -f "$log" "../$artifact"
  "$BASH_BIN" ./tools/configure.sh "$board/$config" 2>&1 | tee "$log"
  "$BASH_BIN" ./dbuild.sh build 2>&1 | tee -a "$log"
  if grep -Eiq "$FAIL_PATTERN" "$log"; then
    echo "Semantic build failure detected: $log" >&2
    return 1
  fi
  if [ ! -s "../$artifact" ]; then
    echo "Expected artifact is missing or empty: $artifact" >&2
    return 1
  fi
  if [ "$(git rev-parse HEAD)" != "$SOURCE_COMMIT" ] ||
     [ -n "$(git status --porcelain=v1 --untracked-files=all)" ]; then
    echo "Source commit or status changed during build" >&2
    return 1
  fi
  shasum -a 256 "../$artifact"
}

build_board rtl8721csm hello build/output/bin/kernel_rtl8721csm_200204.trpk
build_board rtl8730e flat_dev_ddr build/output/bin/fip.bin
build_board bk7239n hello build/output/bin/kernel_bk7239n_200204.trpk

docker ps --all --filter "ancestor=$IMAGE_ID"
LEFTOVER_CONTAINERS="$(docker ps --quiet --all --filter "ancestor=$IMAGE_ID")"
if [ -n "$LEFTOVER_CONTAINERS" ]; then
  docker rm --force $LEFTOVER_CONTAINERS
fi
git status --short
```

Artifact hashes can vary between clean builds. Success means the semantic log scan passes and the expected artifact exists and is non-empty, not that it matches a fixed hash.

ESP32/Xtensa is out of scope for this ARM64 image.
