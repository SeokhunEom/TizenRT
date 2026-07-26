# Mac에서 QEMU ARMv8-M, TASH, `kernel_tc` 실행

이 문서는 Apple Silicon Mac mini에서 TizenRT `qemu-armv8m`을 `tizenrt-2.0.1-arm64` Docker 이미지로 빌드하고, QEMU에서 TASH와 `kernel_tc`를 확인하는 절차다.

## 범위와 전제

- QEMU 머신은 `mps2-an505`, Cortex-M33 계열 ARMv8-M이다.
- 이 문서의 로컬 런타임 검증은 `qemu-armv8m/hello` 설정을 기준으로 한다.
- 실제 RTL/BK 보드의 플래시, UART, 무선, 센서 동작은 이 문서의 QEMU 결과로 검증할 수 없다.
- 저장소 경로가 다르면 아래 `TIZENRT_ROOT`만 현재 checkout 경로로 바꾼다.

## 사전 확인

```bash
export TIZENRT_ROOT="${TIZENRT_ROOT:-/Volumes/T7/Dev/TizenRT/codex/qemu-armv8m-kernel-tc}"
export BASH_BIN="${BASH_BIN:-$(brew --prefix bash)/bin/bash}"

test -x "$BASH_BIN"
"$BASH_BIN" --version | head -1
docker version
docker info --format '{{.Architecture}}'
command -v qemu-system-arm
python3 --version
```

`BASH_BIN`의 major 버전은 4 이상이어야 한다. Docker Desktop이 실행 중이어야 하며, 로컬 ARM64 이미지가 있어야 한다.

```bash
export IMAGE_TAG=tizenrt/tizenrt:2.0.1-arm64-local
docker image inspect "$IMAGE_TAG" >/dev/null
docker image inspect --format '{{.Architecture}}' "$IMAGE_TAG"
```

이미지가 없으면 [AI Build Runbook](AI_Build_Runbook.md)의 이미지 빌드 절차를 먼저 실행한다.

## Docker 플랫폼 자동 선택

`dbuild.sh`가 `docker info`의 architecture를 감지해 Docker platform을 선택한다. Apple Silicon에서는 `linux/arm64`와 local ARM64 image를 사용하고, x86 Docker에서는 `linux/amd64`를 사용한다. 따라서 `DOCKER_DEFAULT_PLATFORM`을 직접 설정하지 않는다.

```bash
cd "$TIZENRT_ROOT/os"
./dbuild.sh menu
```

실행 초기에 다음과 같은 platform 표시가 나오는지 확인한다.

```text
Docker Image : tizenrt/tizenrt:2.0.1-arm64-local
Docker Platform : linux/arm64
```

## `qemu-armv8m/hello` 빌드

메뉴에서 보드와 config를 바꾸면서 clean build하려면 `5. Clean Build and Re-Configure`를 선택하고, `qemu-armv8m`과 `hello`를 다시 선택한 뒤 `1. Build with Current Configuration`을 선택한다. 결과물은 저장소 루트의 `build/output/bin/tinyara`에 생성된다.

```bash
cd "$TIZENRT_ROOT/os"
./dbuild.sh menu
# 5: Clean Build and Re-Configure
# qemu-armv8m -> hello -> 1: Build with Current Configuration

test -s "$TIZENRT_ROOT/build/output/bin/tinyara"
file "$TIZENRT_ROOT/build/output/bin/tinyara"
```

## TASH와 `kernel_tc` 검증

자동화된 runner를 권장한다. runner는 QEMU를 기동하고 새 TASH 프롬프트를 확인한 뒤 `kernel_tc`를 전송한다. 단순히 이전 로그에 PASS 문자열이 있는 것은 성공으로 보지 않으며, 새 실행에서 `PASS > 0` 및 `FAIL : 0`을 요구한다.

```bash
cd "$TIZENRT_ROOT"
mkdir -p build/qemu-armv8m

python3 .github/scripts/qemu-armv8m-kernel-tc.py \
  --config hello \
  --timeout 1200 \
  --log build/qemu-armv8m/hello-kernel-tc.log \
  --result build/qemu-armv8m/hello-kernel-tc.result.json

cat build/qemu-armv8m/hello-kernel-tc.result.json
rg -n "TASH>>|Kernel TC End|PASS|FAIL" \
  build/qemu-armv8m/hello-kernel-tc.log
```

성공 조건은 결과 JSON의 `status`가 `pass`이고, 로그에 다음 형태의 결과가 있는 것이다.

```text
Kernel TC End [PASS : <positive number>, FAIL : 0]
```

지원되는 QEMU 설정은 다음과 같다.

```bash
for config in hello loadable_all loadable_apps xip_all; do
  python3 .github/scripts/qemu-armv8m-kernel-tc.py \
    --config "$config" \
    --timeout 1200 \
    --log "build/qemu-armv8m/${config}-kernel-tc.log" \
    --result "build/qemu-armv8m/${config}-kernel-tc.result.json"
done
```

`hello` 이외 설정은 앱/공통 바이너리 패키지와 CI 조건에 따라 추가 입력이 필요할 수 있다. 따라서 Mac에서 로컬로 검증한 범위와 CI에서 검증한 범위를 결과에 구분해 기록한다.

## 수동 TASH 확인

자동 runner를 사용할 수 없는 경우에는 `qemu-armv8m`의 Makefile이 제공하는 `download` 경로로 QEMU를 실행할 수 있다.

```bash
cd "$TIZENRT_ROOT/os"
PATH="$(brew --prefix)/bin:$PATH" \
  qemu-system-arm -M mps2-an505 -kernel ../build/output/bin/tinyara -nographic
```

TASH 프롬프트가 나오면 다음을 입력한다.

```text
kernel_tc
```

자동 runner의 성공 조건과 동일하게 `Kernel TC End [PASS : n, FAIL : 0]`을 확인한다. 터미널 입력과 QEMU 표준 입출력 연결이 불안정하면 수동 결과 대신 runner 결과를 사용한다.

## 문제 해결

| 증상 | 조치 |
| --- | --- |
| 로컬 `2.0.1-arm64-local` 이미지를 찾지 못함 | `dbuild.sh` 출력의 `Docker Platform : linux/arm64`를 확인하고, 메뉴의 `5. Clean Build and Re-Configure`를 사용한다. |
| `Already configured and compiled` | `./dbuild.sh menu`에서 `5. Clean Build and Re-Configure`를 선택한다. |
| `TASH>>`가 보이지 않음 | `build/output/bin/tinyara`가 새로 생성됐는지, QEMU가 실행 중인지, 로그/timeout을 확인한다. |
| 예전 PASS 로그로 성공 처리됨 | 새 로그와 새 result JSON을 사용한다. runner는 fresh prompt epoch을 요구한다. |
| `kernel_tc`가 실패함 | QEMU 로그의 `PASS`/`FAIL` 수를 확인하고, QEMU 통과를 하드웨어 통과로 해석하지 않는다. |
