# Mac 터미널에서 QEMU ARMv8-M 실행하기

이 문서는 사람이 Apple Silicon Mac mini의 터미널에서 `qemu-armv8m`을 빌드하고 QEMU를 직접 실행한 뒤, TASH 명령과 `kernel_tc`를 입력해 보는 절차다.

이 가이드는 `qemu-armv8m/hello`를 기준으로 한다. `hello`는 TASH와 `kernel_tc`가 포함된 flat 커널 이미지이므로 실물 보드 없이 가장 빠르게 빌드와 부팅을 확인할 수 있다.

## 1. 준비물

다음 프로그램을 준비한다.

- Apple Silicon Mac mini
- Docker Desktop
- Homebrew Bash 4 이상
- QEMU의 `qemu-system-arm`
- Python 3

없다면 한 번만 설치한다.

```bash
brew install bash qemu python
```

Docker Desktop을 실행한 뒤 새 터미널을 열고 도구를 확인한다.

```bash
export PATH="$(brew --prefix)/bin:$PATH"
export BASH_BIN="$(brew --prefix bash)/bin/bash"

docker info --format 'Docker={{.ServerVersion}} Arch={{.Architecture}} CPUs={{.NCPU}}'
"$BASH_BIN" --version | head -1
command -v qemu-system-arm
python3 --version
```

Docker architecture는 `aarch64` 또는 `arm64`여야 한다. `qemu-system-arm`이 출력되지 않으면 QEMU 설치 상태와 `PATH`를 확인한다.

## 2. 저장소와 ARM64 Docker 이미지 설정

저장소 경로가 다르면 첫 줄만 현재 경로로 바꾼다.

```bash
export TIZENRT_ROOT="${TIZENRT_ROOT:-/Volumes/T7/Dev/TizenRT/codex/qemu-armv8m-kernel-tc}"
export IMAGE_TAG="tizenrt/tizenrt:2.0.1-arm64-local"

cd "$TIZENRT_ROOT"
test -d os
```

이미지가 이미 있으면 다음 확인이 성공한다.

```bash
docker image inspect "$IMAGE_TAG" >/dev/null
docker image inspect --format '{{.Architecture}}' "$IMAGE_TAG"
```

이미지가 없으면 저장소 루트에서 ARM64 이미지를 만든다.

```bash
export TIZENRT_DOCKER_HOME="$(mktemp -d)"
mkdir -p "$TIZENRT_DOCKER_HOME/.docker"
printf '{}\n' > "$TIZENRT_DOCKER_HOME/.docker/config.json"
export TIZENRT_DOCKER_CONFIG="$TIZENRT_DOCKER_HOME/.docker"
export TIZENRT_DOCKER_HOST="unix://${HOME}/.docker/run/docker.sock"

env \
  HOME="$TIZENRT_DOCKER_HOME" \
  DOCKER_CONFIG="$TIZENRT_DOCKER_CONFIG" \
  DOCKER_HOST="$TIZENRT_DOCKER_HOST" \
  docker buildx build \
    --platform linux/arm64 \
    -t "$IMAGE_TAG" \
    --load \
    tools/docker/tizenrt-2.0.1-arm64
```

Docker socket을 찾지 못하면 Docker Desktop이 실행 중인지 확인한다.

```bash
test -S "$HOME/.docker/run/docker.sock"
```

## 3. `dbuild.sh menu`에서 보드와 config 선택

이 가이드는 개별 `configure.sh`를 직접 조합하지 않고, 사람이 메뉴에서 보드/config와 clean/build를 선택하는 흐름을 기준으로 한다.

```bash
cd "$TIZENRT_ROOT/os"
./dbuild.sh menu
```

보드와 config를 처음 선택하면 `Select build Option`이 표시된다.

```text
1. Build with Current Configuration
2. Re-configure
3. Modify Current Configuration
4. Clean Build
5. Clean Build and Re-Configure
6. Build SmartFS Image
t. Build Test
x. Exit
```

선택 의미는 다음과 같다.

- `1`: 현재 config로 빌드
- `4`: 현재 config의 출력만 `clean`
- `5`: `distclean` 후 보드/config를 다시 선택

보드/config를 바꾸면서 완전히 정리하려면 `5`를 선택하고, 다시 `qemu-armv8m`과 `hello`를 선택한 뒤 `1`을 선택한다. 같은 config를 다시 빌드하려면 `1`, 출력만 지우려면 `4`를 사용한다.

`dbuild.sh`는 Docker architecture를 감지해 `Docker Platform`을 자동으로 선택하고, 실제 `docker run`에 `--platform`을 전달한다. 따라서 `DOCKER_DEFAULT_PLATFORM`을 직접 설정하지 않아도 된다.

```text
Docker Image : tizenrt/tizenrt:2.0.1-arm64-local
Docker Platform : linux/arm64
```

## 4. `qemu-armv8m/hello` 빌드

메뉴 입력의 개념 예시는 다음과 같다. 실제 번호는 화면에 표시된 번호를 사용한다.

```text
./dbuild.sh menu
<보드 선택: qemu-armv8m>
<config 선택: hello>
<빌드 옵션: 1>
```

`5. Clean Build and Re-Configure`를 먼저 선택한 경우에는 다음 순서가 된다.

```text
./dbuild.sh menu
5
<보드 선택: qemu-armv8m>
<config 선택: hello>
1
```

빌드가 끝나면 커널 이미지가 생성됐는지 확인한다.

```bash
test -s "$TIZENRT_ROOT/build/output/bin/tinyara"
ls -lh "$TIZENRT_ROOT/build/output/bin/tinyara"
file "$TIZENRT_ROOT/build/output/bin/tinyara"
```

정상적인 `file` 결과는 ARM 32-bit ELF executable이다.

## 5. QEMU를 현재 터미널에서 직접 실행

QEMU를 터미널에 연결한다.

```bash
export QEMU_BIN="$(command -v qemu-system-arm)"
cd "$TIZENRT_ROOT/os"
"$QEMU_BIN" \
  -M mps2-an505 \
  -kernel ../build/output/bin/tinyara \
  -nographic
```

부팅이 되면 다음과 같이 `TASH>>` 프롬프트가 나온다.

```text
System Information:
...
TASH>>
```

화면에 출력이 멈춘 것처럼 보여도 프롬프트가 있으면 입력할 수 있다.

## 6. TASH 명령 입력

먼저 TASH 명령 목록을 확인한다.

```text
help
```

기본 상태를 확인하려면 다음 명령을 하나씩 입력한다.

```text
free
ps
uptime
```

각 명령 뒤에 다시 `TASH>>`가 나타날 때 다음 명령을 입력한다.

## 7. `kernel_tc` 실행

TASH 프롬프트에서 다음을 입력한다.

```text
kernel_tc
```

테스트가 끝날 때까지 기다린다. 성공 기준은 다음 두 가지를 모두 만족하는 것이다.

- `PASS` 개수가 0보다 큼
- `FAIL : 0`

최종 출력 예:

```text
########## Kernel TC End [PASS : 458, FAIL : 0] ##########
```

`PASS`가 일부 보인다는 이유만으로 성공 처리하지 말고, 반드시 마지막 `Kernel TC End` 줄을 확인한다.

## 8. QEMU 종료

TASH에서 `exit`가 동작하는 설정이면 다음을 입력할 수 있다.

```text
exit
```

QEMU가 계속 실행 중이면 QEMU 터미널 단축키를 사용한다.

1. `Ctrl-A`를 누른다.
2. `Ctrl`과 `A`를 놓는다.
3. `x`를 누른다.

터미널에 `QEMU: Terminated`가 출력되면 종료된 것이다.

## `make download`가 실패할 때

현재 저장소의 `qemu-armv8m/Make.defs`는 macOS에서 `make download`를 실행할 때 다음 문제가 나타날 수 있다.

```text
make: nproc: Command not found
/bin/sh: @qemu-system-arm: command not found
```

`nproc`는 Linux 명령이고, 현재 Makefile의 QEMU 호출에는 recipe용 `@`가 남아 있기 때문이다. 이 경우 소스 파일을 수정하지 않고 이 문서의 직접 실행 명령을 사용한다.

```bash
cd "$TIZENRT_ROOT/os"
PATH="$(brew --prefix)/bin:$PATH" \
  qemu-system-arm -M mps2-an505 -kernel ../build/output/bin/tinyara -nographic
```

## 다른 QEMU config

지원 config는 다음 네 가지다.

```text
hello
loadable_all
loadable_apps
xip_all
```

config를 바꾸려면 `./dbuild.sh menu`에서 `5. Clean Build and Re-Configure`를 선택하고, 보드/config를 다시 고른 뒤 `1. Build with Current Configuration`을 선택한다.

```bash
cd "$TIZENRT_ROOT/os"
./dbuild.sh menu
```

`loadable_all`과 `loadable_apps`는 `app1`, `xip_all`은 `app1`과 `common` 패키지도 필요하다. 사람이 터미널에서 먼저 확인할 대상은 `hello`이며, loadable/XIP 패키지의 로딩과 거부 케이스는 [QEMU ARMv8-M 기술 문서](../AI/Mac_QEMU_ARMv8M_TASH_KernelTC.md)와 저장소 runner/CI 절차를 따른다.

## 문제 해결

| 증상 | 확인할 내용 |
| --- | --- |
| `qemu-system-arm: command not found` | `brew install qemu` 후 `export PATH="$(brew --prefix)/bin:$PATH"`를 실행한다. |
| Docker 이미지가 없음 | `docker image inspect "$IMAGE_TAG"`를 확인하고 ARM64 이미지 build 절차를 실행한다. |
| 잘못된 platform으로 registry를 찾음 | `dbuild.sh`가 `Docker Platform : linux/arm64`를 출력하는지 확인한다. 스크립트가 수정된 checkout인지도 확인한다. |
| `Already configured and compiled` | `./dbuild.sh menu`에서 `5. Clean Build and Re-Configure`를 선택한다. |
| `TASH>>`가 안 나옴 | `tinyara`가 새로 생성됐는지, QEMU 프로세스가 남아 있지 않은지 확인한다. |
| `kernel_tc`가 멈춘 것처럼 보임 | 전체 suite가 실행 중일 수 있으므로 마지막 `Kernel TC End`까지 기다린다. |
| `FAIL`이 1개 이상임 | 마지막 결과와 앞선 실패 testcase를 기록하고, 빌드 config와 변경사항을 확인한다. |

## 직접 검증 기록

이 가이드는 2026-07-23에 다음 순서로 직접 확인했다.

1. `qemu-armv8m/hello`를 ARM64 Docker 이미지로 clean build
2. `qemu-system-arm -M mps2-an505 ... -nographic`로 현재 터미널에서 부팅
3. TASH에서 `help` 실행
4. TASH에서 `kernel_tc` 실행
5. `Kernel TC End [PASS : 458, FAIL : 0]` 확인

QEMU 결과는 QEMU 소프트웨어 경로의 검증 결과이며, 실제 보드의 주변장치나 하드웨어 동작을 의미하지 않는다.
