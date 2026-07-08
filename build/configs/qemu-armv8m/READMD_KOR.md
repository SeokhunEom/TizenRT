# QEMU ARMv8-M MPS2-AN505 사용 가이드

이 문서는 `qemu-armv8m` 보드에서 TizenRT를 빌드하고 QEMU로 실행하는
방법을 정리합니다. 대상 QEMU 머신은 Cortex-M33 기반의 `mps2-an505`입니다.

## 지원 config

| Config | 빌드 모델 | 런타임 이미지 구성 |
| --- | --- | --- |
| `kernel_tc` | 기존 kernel TC용 flat 커널 이미지 | 커널 이미지만 사용 |
| `hello` | TASH와 `kernel_tc`가 커널에 포함된 flat 빌드 | 커널 이미지만 사용 |
| `loadable_all` | protected 빌드와 loadable ELF 앱 패키지 | 커널 이미지 + `app1` |
| `loadable_apps` | XIP 커널과 loadable ELF 앱 패키지 | 커널 이미지 + `app1` |
| `xip_all` | XIP 커널과 XIP ELF common/app 패키지 | 커널 이미지 + `common` + `app1` |

## 핵심 수정사항

이번 qemu-armv8m 포팅은 `bk7239n`의 4개 앱 구성과 유사한 형태를
QEMU MPS2-AN505 메모리 맵 위에서 동작하도록 맞춘 것입니다.

주요 내용은 다음과 같습니다.

- `hello`, `loadable_all`, `loadable_apps`, `xip_all` defconfig를 추가했습니다.
- loadable 구성에서 QEMU loader로 앱 패키지를 SSRAM 영역에 배치합니다.
- board 초기화 시 QEMU가 올린 패키지를 tmpfs로 복사한 뒤 binfmt 경로로 실행합니다.
- `xip_all`에서는 `common`과 `app1`을 각각 다른 주소에 올려 XIP ELF 레이아웃을 맞춥니다.
- binary manager 없이도 app separation에 필요한 최소 binfmt/header/tool 경로가 동작하도록 보강했습니다.
- `timer_gettime()` TC는 남은 시간이 tick 단위로 반환되는 특성을 반영해 1 tick 허용 오차를 둡니다.

loadable 주소 규칙은 다음과 같습니다.

- `loadable_all`, `loadable_apps`: `app1`을 `0x10300000`에 로드합니다.
- `xip_all`: `common`을 `0x102c0000`, `app1`을 `0x10360000`에 로드합니다.

## 사전 준비

다음 도구가 필요합니다.

- `qemu-system-arm`
- `mps2-an505` 머신을 지원하는 QEMU
- `arm-none-eabi-gcc`, `arm-none-eabi-nm`, `arm-none-eabi-readelf` 등 ARM embedded toolchain

native toolchain을 사용할 때는 빌드 전에 PATH에 추가합니다.

```sh
export PATH=/path/to/arm-none-eabi/bin:$PATH
```

## 빌드 방법

저장소 루트에서 실행합니다.

```sh
cd os
make distclean
./tools/configure.sh qemu-armv8m/hello
make
```

다른 config를 빌드하려면 `hello` 대신 원하는 config를 지정합니다.

```sh
./tools/configure.sh qemu-armv8m/kernel_tc
./tools/configure.sh qemu-armv8m/loadable_all
./tools/configure.sh qemu-armv8m/loadable_apps
./tools/configure.sh qemu-armv8m/xip_all
```

config를 바꿀 때는 먼저 `make distclean`을 실행해야 합니다.
기존 `.config`가 남아 있으면 `configure.sh`가 덮어쓰지 않습니다.

공통 커널 산출물은 다음 위치에 생성됩니다.

```text
../build/output/bin/tinyara
```

loadable 구성에서는 앱 패키지도 생성됩니다.

```text
../build/output/bin/app1
```

`xip_all`에서는 common 패키지가 추가로 생성됩니다.

```text
../build/output/bin/common
```

## `hello` 실행

`hello`는 flat 이미지라 커널 이미지만 QEMU에 전달하면 됩니다.

```sh
qemu-system-arm \
  -M mps2-an505 \
  -kernel ../build/output/bin/tinyara \
  -nographic
```

TASH 프롬프트가 나오면 다음 명령을 실행합니다.

```text
kernel_tc
```

## `loadable_all`, `loadable_apps` 실행

`qemu-armv8m/loadable_all` 또는 `qemu-armv8m/loadable_apps`를 빌드한 뒤
`app1` 패키지를 QEMU loader로 함께 올립니다.

```sh
qemu-system-arm \
  -M mps2-an505 \
  -kernel ../build/output/bin/tinyara \
  -device loader,file=../build/output/bin/app1,addr=0x10300000,force-raw=on \
  -nographic
```

TASH 프롬프트에서 실행합니다.

```text
kernel_tc
```

## `xip_all` 실행

`qemu-armv8m/xip_all`은 `common`과 `app1` 패키지를 모두 로드해야 합니다.

```sh
qemu-system-arm \
  -M mps2-an505 \
  -kernel ../build/output/bin/tinyara \
  -device loader,file=../build/output/bin/common,addr=0x102c0000,force-raw=on \
  -device loader,file=../build/output/bin/app1,addr=0x10360000,force-raw=on \
  -nographic
```

TASH 프롬프트에서 실행합니다.

```text
kernel_tc
```

## Console 문제 해결

이 문서의 QEMU 명령은 `os` 디렉터리에서 실행하는 것을 기준으로 합니다.
저장소 루트에서 실행한다면 `../build/output/bin/tinyara` 대신
`build/output/bin/tinyara`를 사용해야 합니다.

콘솔에 같은 `System Information` 블록이 계속 출력되거나 마지막 TASH 명령이
반복 실행되는 것처럼 보이면, 먼저 QEMU가 닫힌 stdin 또는 비대화형 stdin으로
실행 중인지 확인합니다. 예를 들어 `-display none -serial stdio -monitor none`
조합에 pipe로 입력을 한 번만 넣으면, stdin EOF 이후 QEMU stdio character
device가 TASH로 NUL 바이트를 반복 전달할 수 있습니다. 현재 TASH 입력 루프는
NUL 바이트를 버리지 않기 때문에 serial 로그가 같은 명령을 계속 실행하는 것처럼
보일 수 있습니다.

로컬에서 직접 실행할 때는 board의 `download` target 또는 위의 `-nographic`
명령을 우선 사용합니다.

```sh
make download
```

스크립트나 분리된 터미널에서 실행해야 한다면, 유한한 stdin stream을
`-serial stdio`에 직접 pipe하지 말고 serial backend를 열린 상태로 유지합니다.
예를 들어 PTY를 사용할 수 있습니다.

```sh
qemu-system-arm \
  -M mps2-an505 \
  -kernel ../build/output/bin/tinyara \
  -display none \
  -serial pty \
  -monitor none
```

QEMU는 다음과 비슷한 줄을 출력합니다.

```text
char device redirected to /dev/pts/7 (label serial0)
```

다른 Linux 터미널을 열고 출력된 PTY 경로에 접속합니다. `picocom`이 설치되어
있다면 가장 간단합니다.

```sh
picocom -b 115200 /dev/pts/7
```

`picocom` 종료는 `Ctrl-A`를 누른 뒤 `Ctrl-X`를 누릅니다.

`picocom`이 없다면 `screen`도 사용할 수 있습니다.

```sh
screen /dev/pts/7 115200
```

`screen` 종료는 `Ctrl-A`를 누른 뒤 `K`, `y`를 순서대로 누릅니다.

별도 serial tool 없이 최소 확인만 하려면 한 터미널에서 PTY를 읽고, 다른
터미널에서 명령을 씁니다.

```sh
cat /dev/pts/7
```

```sh
printf 'ps\n' > /dev/pts/7
```

`/dev/pts/7`은 QEMU가 실제로 출력한 경로로 바꿔 사용합니다.

## 검증된 `kernel_tc` 결과

최종 검증 결과는 다음과 같습니다.

```text
qemu-armv8m/kernel_tc     Kernel TC End [PASS : 449, FAIL : 0]
qemu-armv8m/hello         Kernel TC End [PASS : 449, FAIL : 0]
qemu-armv8m/loadable_all  Kernel TC End [PASS : 447, FAIL : 0]
qemu-armv8m/loadable_apps Kernel TC End [PASS : 447, FAIL : 0]
qemu-armv8m/xip_all       Kernel TC End [PASS : 447, FAIL : 0]
```

os-api-test 계열 kernel TC(group/irq/pipe/procfs/vfs)를 추가로 활성화했습니다.
이를 위해 5개 defconfig에 다음을 추가했습니다.

```text
CONFIG_SCHED_CHILD_STATUS=y      # TC_KERNEL_GROUP 의존성
CONFIG_TC_KERNEL_GROUP=y
CONFIG_TC_KERNEL_IRQ=y
CONFIG_TC_KERNEL_PIPE=y
CONFIG_TC_KERNEL_PROCFS=y
CONFIG_TC_KERNEL_VFS=y
CONFIG_FS_AUTOMOUNT_PROCFS=y     # procfs TC 실행에 필요한 /proc 자동 마운트
```

RTC/PM/NET/WATCHDOG/LOG_DUMP/REBOOT_REASON/BINARY_MANAGER/MEM_LEAK_CHECKER 계열
TC는 해당 서브시스템이 qemu-armv8m 구성에 없어 Kconfig 의존성에 의해 자동 제외됩니다.

flat 구성과 protected/loadable 구성의 PASS 개수가 다른 이유는 빌드 모델과
활성화된 기능에 따라 일부 TC가 조건부로 제외되기 때문입니다.

## GitHub Actions CI

`qemu-armv8m` GitHub Actions workflow는 지원하는 모든 defconfig를 빌드한 뒤
QEMU에서 `kernel_tc`를 실행합니다.

```text
kernel_tc, hello, loadable_all, loadable_apps, xip_all
```

CI는 `tizenrt/tizenrt:1.5.8` 빌드 이미지를 사용하고 runner에
`qemu-system-arm`을 설치합니다. 각 config의 QEMU serial 로그는 artifact로
업로드됩니다. 로컬에서 빌드가 끝난 뒤에는 저장소 루트에서 같은 helper를
사용할 수 있습니다.

```sh
python3 .github/scripts/qemu-armv8m-kernel-tc.py --config hello --timeout 600
```

## 참고

- `-display none -serial stdio -monitor none` 조합은 제어 프로세스가 stdin을
  계속 열어 두고 serial stream을 정상적으로 소비하는 자동화 환경에서만
  사용합니다. 수동 실행에는 `-nographic`을 우선 사용합니다.
- loadable 주소는 QEMU board와 linker/package layout 사이의 약속입니다.
  linker script나 패키지 배치가 바뀌면 QEMU 실행 명령과
  `os/board/qemu-armv8m/src/qemu_armv8m_boot.c`를 함께 갱신해야 합니다.
- `xip_all`에서 `common`을 빼고 `app1`만 올리면 앱 엔트리와 common symbol 배치가
  맞지 않아 런타임 fault나 예상치 못한 IRQ assertion이 발생할 수 있습니다.
