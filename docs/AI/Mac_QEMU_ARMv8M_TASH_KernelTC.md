# Mac에서 QEMU ARMv8-M, TASH, `kernel_tc` 실행

이 문서는 Apple Silicon Mac mini에서 TizenRT `qemu-armv8m`을 `tizenrt-2.0.1-arm64` Docker 이미지로 빌드하고, QEMU에서 TASH와 `kernel_tc`를 확인하는 절차다.

## 범위와 전제

- QEMU 머신은 `mps2-an505`, Cortex-M33 계열 ARMv8-M이다.
- `hello`, `loadable_all`, `loadable_apps`, `xip_all`은 각각 clean build,
  TASH 진입, Ethernet 네트워크와 full `network_tc`/`kernel_tc`를 로컬에서
  확인한다.
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

실제 build가 `/Volumes/T7` exFAT bind mount의 일시적 copy race로 실패하고
`os/build.log`에 `as it was replaced while being copied`가 있으면
`dbuild.sh`가 같은 build를 정확히 한 번 자동 재시도한다. marker가 없는
compiler/linker 오류와 `clean`/`distclean`/`menuconfig`는 재시도하지 않는다.
marker와 다른 오류가 같은 로그에 있으면 한 번만 재시도하며, 두 번째 실패의
종료 코드는 그대로 반환한다.

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

## QEMU Ethernet과 TASH 검증

MPS2-AN505의 LAN9118 MMIO NIC를 `eth0`으로 등록한다. runner는 QEMU
user-mode network를 다음 고정 계약으로 시작한다.

- guest MAC: `52:54:00:12:34:56`
- IPv4 guest/host: DHCP `10.0.2.15` / gateway `10.0.2.2`
- IPv6 guest/host: `fec0::15` / `fec0::2`
- 필수 Internet 확인: `example.com` DNS 조회
- 관찰 항목: `1.1.1.1` ICMP. QEMU user-mode network와 host 방화벽에 따라
  0/1이어도 DNS가 성공하면 네트워크 실패로 처리하지 않는다.

runner는 명령 등록을 확인한 뒤 다음 순서로 실행한다.

```text
help
ifdown eth0
ifup eth0
ifconfig eth0 dhcp
ifconfig eth0 fec0::15
ifconfig eth0
ping -c 3 10.0.2.2
ping6 -c 3 fec0::2
netdb --host example.com
ping -c 1 1.1.1.1
net_stats
network_tc
sleep 2
kernel_tc
```

필수 성공 조건은 DHCP 주소 획득, IPv4/IPv6 gateway ping 수신, DNS 결과,
`Network TC End [PASS : n, FAIL : 0]`, `Kernel TC End [PASS : n, FAIL : 0]`
이다. `network_tc`의 보조 thread가 종료될 시간을 확보하기 위해 두 suite
사이에 2초를 둔다. Ethernet-only build의 `ifconfig`는 IPv4/MAC/MTU를
표시하며, IPv6 동작은 `ping6` 결과로 확인한다.

## 자동 runner로 `network_tc`와 `kernel_tc` 검증

자동화된 runner를 권장한다. runner는 QEMU를 기동하고 새 TASH 프롬프트를
확인한 뒤 위 네트워크 절차와 두 testcase suite를 실행한다. 단순히 이전
로그에 PASS 문자열이 있는 것은 성공으로 보지 않으며, 새 실행에서 각
suite의 `PASS > 0` 및 `FAIL : 0`을 요구한다.
완전한 `Assertion failed at file:... line:...` 진단이 나타나면 최종 집계나
timeout을 기다리지 않고 `reason=kernel-assert`로 즉시 실패한다. serial
chunk 중간의 잘린 prefix만으로는 QEMU를 종료하지 않아 file/line을 보존한다.

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

완전한 성공 조건은 결과 JSON의 최상위 `status`와 `network.status`가
모두 `pass`이고, 로그에 다음 두 결과가 있는 것이다.

```text
Network TC End [PASS : <positive number>, FAIL : 0]
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

| config | 네트워크 결과 | 커널 결과 | 제한 사항 |
| --- | --- | --- |
| `hello` | LAN9118, IPv4/IPv6/DNS, `161/0` | `459/0` | QEMU flat/multiheap 경로 |
| `loadable_all` | LAN9118, IPv4/IPv6/DNS, `161/0` | `447/0` | RAM-backed package 경로 |
| `loadable_apps` | LAN9118, IPv4/IPv6/DNS, `161/0` | `447/0` | 큰 app SRAM recipe |
| `xip_all` | LAN9118, IPv4/IPv6/DNS, `161/0` | `447/0` | PI-on/XIP 경로 |

각 clean build에서 생성된 `.config`의 network/netdev/network-test 설정
118개를 추출해 줄 번호를 제외하고 비교한 SHA-256은 네 recipe 모두
`0f243d064d2192ff842216941298a523131b97c6616151af02bff845bfb6cea8`로
같았다. 즉 defconfig 선언뿐 아니라 Kconfig 해석 결과도 동일한 네트워크
계약이다.

세마포어 holder 정책도 recipe 역할에 맞춘다. `hello`는 PI와 Binary
Manager가 모두 꺼져 holder tracking을 빌드하지 않는다. PI-off
`loadable_all`과 `loadable_apps`는 Binary Manager holder recovery용으로
preallocated holder 16개를 사용한다. PI-on `xip_all`도 같은 크기를
priority inheritance와 Binary Manager에 사용한다. pool이 가득 찬
single-count waiter handoff에서는 해제된 zero-count holder를 직접
재사용하고, multi-count handoff에서는 마지막 남은 pool slot에 waiter
holder가 등록되는 경계를 회귀 테스트한다.

CI의 positive/negative matrix와 artifact는 별도 검증 축이다.
위 표의 runtime 수치는 이 checkout에서 생성한 로그를 바탕으로 한 세션
기록(2026-07-26)이며, generated log/result는 커밋하지 않는다. 재현 시에는 매번 새
`--log`와 `--result` 경로를 지정한다.
일반 positive 결과는 `active_slot=0`, `attempt=0`이다. 별도
`loadable_all` recovery에서는 손상된 A-slot `app1`을 거부하고 bootparam
version 1→2, active slot 0→1로 전환한 뒤 B-slot에서 network `161/0`과
kernel `447/0`까지 통과했다.

## 수동 TASH 확인

자동 runner를 사용할 수 없는 경우에는 `qemu-armv8m`의 Makefile이 제공하는 `download` 경로로 QEMU를 실행할 수 있다.

```bash
cd "$TIZENRT_ROOT/os"
PATH="$(brew --prefix)/bin:$PATH" \
  qemu-system-arm \
    -M mps2-an505 \
    -kernel ../build/output/bin/tinyara \
    -nic user,ipv4=on,ipv6=on,net=10.0.2.0/24,host=10.0.2.2,ipv6-net=fec0::/64,ipv6-host=fec0::2,mac=52:54:00:12:34:56 \
    -nographic
```

TASH 프롬프트가 나오면 다음을 입력한다.

```text
help
ps
mount
ls /mnt
smartfs_test 4 /mnt/test 4096 1 y
ifconfig eth0 dhcp
ifconfig eth0 fec0::15
ping -c 3 10.0.2.2
ping6 -c 3 fec0::2
netdb --host example.com
network_tc
kernel_tc
```

`mount` 출력에 `/mnt type smartfs`가 보이고 `smartfs_test`가 `run_time`을
출력하면 QEMU RAM-backed SmartFS와 TASH 명령이 동작한 것이다. QEMU
`hello`는 `CONFIG_RAMMTD_ERASE_ON_INIT=y`이므로 재부팅하면 SmartFS 내용이
초기화된다. 첫 `TASH>>` 직후 명령이 `not registered`이면 `help`를 입력해
등록 완료를 확인한 뒤 다시 시도한다.

자동 runner의 성공 조건과 동일하게 `Kernel TC End [PASS : n, FAIL : 0]`을 확인한다. 터미널 입력과 QEMU 표준 입출력 연결이 불안정하면 수동 결과 대신 runner 결과를 사용한다.

## 문제 해결

| 증상 | 조치 |
| --- | --- |
| 로컬 `2.0.1-arm64-local` 이미지를 찾지 못함 | `dbuild.sh` 출력의 `Docker Platform : linux/arm64`를 확인하고, 메뉴의 `5. Clean Build and Re-Configure`를 사용한다. |
| `Already configured and compiled` | `./dbuild.sh menu`에서 `5. Clean Build and Re-Configure`를 선택한다. |
| `TASH>>`가 보이지 않음 | `build/output/bin/tinyara`가 새로 생성됐는지, QEMU가 실행 중인지, 로그/timeout을 확인한다. |
| 예전 PASS 로그로 성공 처리됨 | 새 로그와 새 result JSON을 사용한다. runner는 fresh prompt epoch을 요구한다. |
| result의 `reason`이 `kernel-assert` | 로그의 첫 `Assertion failed at file:`과 call stack을 확인한다. timeout으로 재실행해 덮지 않는다. |
| `kernel_tc`가 실패함 | QEMU 로그의 `PASS`/`FAIL` 수를 확인하고, QEMU 통과를 하드웨어 통과로 해석하지 않는다. |
