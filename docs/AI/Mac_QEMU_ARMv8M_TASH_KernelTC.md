# Mac에서 QEMU ARMv8-M, TASH, `kernel_tc` 실행

이 문서는 Apple Silicon Mac mini에서 TizenRT `qemu-armv8m`을 `tizenrt-2.0.1-arm64` Docker 이미지로 빌드하고, QEMU에서 TASH와 `kernel_tc`를 확인하는 절차다.

## 범위와 전제

- QEMU 머신은 `mps2-an505`, Cortex-M33 계열 ARMv8-M이다.
- `hello`, `loadable_all`, `loadable_apps`, `xip_all`은 로컬 빌드/부팅 smoke를
  확인했다. `loadable_apps`는 common/app1/app2를 로드하고 TASH까지 진입한
  뒤 semaphore holder assertion으로 timeout되었다.
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

## SRAM heap과 메모리 레이아웃

QEMU의 MPS2-AN505 SSRAM 상단 512 KiB를 별도 heap으로 예약한다. linker
script는 SSRAM 코드 영역을 `0x10000000`부터 `0x10380000`까지 사용하고,
`0x10380000`부터 `0x10400000`까지를 heap으로 남긴다.

| config 계열 | main RAM heap | SSRAM heap | heap index |
| --- | --- | --- | --- |
| `hello` | `0x80000000`, 12 MiB | `0x10380000`, 512 KiB | `0`, `1` |
| loadable/XIP | `0x80000000`, 4 MiB + `0x80400000`, 8 MiB | `0x10380000`, 512 KiB | `0`, `2`, `1` |

loadable/XIP는 이 레이아웃을 공통 기반으로 사용한다. 첫 main RAM 영역은
커널과 flash-backed 작업에, 두 번째 영역은 loaded app과 동적 메모리에,
SSRAM 영역은 보조 heap에 할당한다. 이 경계를 바꾸면 linker script,
defconfig의 `CONFIG_RAM_KREGIONx_*`, 패키지 runner를 함께 검증해야 한다.

## RAM-backed flash와 A/B state

loadable/XIP runner는 main RAM 상단의 4 MiB를 RAM-backed flash로 사용하고,
kernel/common/app 슬롯 두 세트와 8 KiB boot parameter를 state image에
보관한다. 실행마다 임시 state를 만들 수 있고, 다음처럼 `--state-image`를
지정하면 같은 boot parameter와 active slot을 재사용할 수 있다.

```bash
cd "$TIZENRT_ROOT"
python3 .github/scripts/qemu-armv8m-kernel-tc.py \
  --config loadable_all \
  --state-image build/qemu-armv8m/qemu.state \
  --max-reboots 1 \
  --timeout 1200 \
  --log build/qemu-armv8m/loadable_all-kernel-tc.log \
  --result build/qemu-armv8m/loadable_all-kernel-tc.result.json
```

`--max-reboots 1`은 Binary Manager가 bootparam을 갱신한 뒤 clean reboot로
종료한 경우에만 alternate slot을 한 번 재시도한다. timeout, kernel-tc
실패, protocol 오류는 다른 slot으로 바꾸지 않고 실패로 남긴다.
패키지를 다시 빌드하면 기존 state가 이전 산출물을 유지할 수 있으므로,
새 패키지로 시작하려면 state image를 삭제한다. `hello`는 flat kernel
경로이므로 이 A/B staging 경로를 사용하지 않는다.

손상/누락 package의 negative runtime은 수동 로더의
`QEMU_LOAD_REJECT` 문자열이 아니라 Binary Manager의
`binary_manager_load: Invalid Header data, name : common/app1` 진단을
검증한다. Binary Manager가 recovery reboot로 프로세스를 종료한 뒤에도
해당 진단과 같은 binary의 alternate-slot 성공 진단
(`binary_manager_load: common Header Checking Success` 또는
`binary_manager_load: app1 Header Checking Success`) 부재가 확인되면
runner는 `"status": "expected-rejection"`으로 기록한다.

명시한 state image로 corrupt `common`을 실행한 로컬 증거에서는 유효한
BP0(version 1, active A)을 보존하고 BP1(version 2, active B)에 복구 결과를
기록한 뒤 clean reboot를 요청했다. 이 결과는 A/B bootparam 전환 증거이며,
full `kernel_tc` 성공을 의미하지 않는다.

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

완전한 `kernel_tc` 성공 조건은 결과 JSON의 `status`가 `pass`이고, 로그에
다음 형태의 결과가 있는 것이다.

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

`loadable_all`, `loadable_apps`, `xip_all`도 같은 runner 인터페이스를
사용한다. loadable/XIP 실행에서는 runner가 package와 bootparam state를
준비하므로 raw QEMU 명령을 별도로 조립하지 않는다.

## 이번 구현에서 확인한 결과

검증 결과는 full `kernel_tc` pass와 boot/package smoke를 구분한다.

| config | 확인 결과 | 제한 사항 |
| --- | --- | --- |
| `hello` | build와 TASH/`kernel_tc` 실행 확인, `PASS : 457`, `FAIL : 2` | semaphore와 multiheap 관련 2건 실패 |
| `loadable_all` | build, binary-manager의 common/app1/app2 탐색과 TASH 부팅 확인; A/B state staging/bootparam 단위 검증 | `kernel_tc`가 semaphore holder assertion 뒤 timeout |
| `xip_all` | build, flash-backed common/app1 XIP 배치와 TASH 부팅 확인 | scheduler testcase 뒤 full `kernel_tc` timeout |
| `loadable_apps` | build, binary-manager의 common/app1/app2 탐색과 TASH 부팅 확인 | semaphore holder assertion 뒤 timeout |

따라서 현재 상태를 “모든 QEMU config의 `FAIL : 0`”으로 보고하지 않는다.
CI의 positive/negative matrix와 artifact는 별도 검증 축이다.
위 표의 runtime 수치는 이 checkout에서 생성한 로그를 바탕으로 한 세션
기록이며, generated log/result는 커밋하지 않는다. 재현 시에는 매번 새
`--log`와 `--result` 경로를 지정한다.

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
