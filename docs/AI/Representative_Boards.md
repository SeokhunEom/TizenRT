# 대표 보드와 빌드 레시피

이 문서는 AI 개발 작업에서 먼저 선택할 대표 보드와 config를 정리한다. 사용자가 말한 `bk2739n`은 이 저장소의 실제 보드 디렉터리명인 `bk7239n`으로 해석한다.

## 한눈에 보기

| 보드 | 아키텍처/플랫폼 | 시작 recipe | 대표 출력 | 로컬 실행 |
| --- | --- | --- | --- | --- |
| `rtl8730e` | ARMv7-A 계열, Realtek AmebaSmart | `flat`, `loadable_apps`, `loadable_ext_ddr_st7785` | `build/output/bin/fip.bin` | 하드웨어 필요 |
| `bk7239n` | ARMv8-M Cortex-M33, Armino | `hello`, `xip_all` | `kernel_bk7239n_200204.trpk` 또는 package 출력 | 하드웨어 필요 |
| `rtl8721csm` | ARMv8-M Cortex-M33, Realtek AmebaD | `hello`, `loadable_apps` | `kernel_rtl8721csm_200204.trpk` 등 | 하드웨어 필요 |
| `qemu-armv8m` | QEMU `mps2-an505`, Cortex-M33 | `hello`, `xip_all` | `build/output/bin/tinyara` 또는 XIP package | QEMU/TASH/`kernel_tc` |

모든 출력 경로는 저장소 루트 기준이다. `os/`에서 명령을 실행할 때는 artifact 경로 앞에 `../`가 필요하다.

## 공통 빌드 형식

Mac mini에서는 [AI Build Runbook](AI_Build_Runbook.md)의 `./dbuild.sh menu` 흐름을 사용한다. 보드와 config를 바꾸면서 완전히 정리하려면 메뉴의 `5. Clean Build and Re-Configure`를 선택하고, 다시 보드/config를 고른 뒤 `1. Build with Current Configuration`을 선택한다.

```bash
cd "$TIZENRT_ROOT/os"
./dbuild.sh menu
```

`dbuild.sh`가 Docker architecture를 감지해 `--platform linux/arm64` 또는 `--platform linux/amd64`를 자동으로 전달하므로 `DOCKER_DEFAULT_PLATFORM`을 별도로 설정하지 않는다.

## `rtl8730e`

### 특징

- Realtek AmebaSmart 기반 ARMv7-A 계열 보드다.
- `rtl8730e_make_bin.sh`와 `fiptool`을 사용해 최종 패키지를 만든다.
- `flat_*`는 단일 flat 이미지 계열이고, `loadable_*`는 kernel/app 분리 계열이다.

### 추천 시작 recipe

| config | 용도 | 주요 출력/특징 |
| --- | --- | --- |
| `flat` | 가장 단순한 기본 빌드와 flat 계열 확인 | 저장소의 구체적인 시작 config는 `flat_dev_ddr`; `fip.bin` |
| `loadable_apps` | kernel과 앱 분리, protected/loadable 개발 | app binary separation, common binary 지원 |
| `loadable_ext_ddr_st7785` | 외부 DDR 및 ST7785 display loadable 구성 | DDR/display 기반 메모리 및 앱 구성 |

기본 build smoke test는 메뉴에서 `rtl8730e`와 `flat_dev_ddr`를 선택해 실행한다.

```bash
./dbuild.sh menu
# 5: Clean Build and Re-Configure
# rtl8730e -> flat_dev_ddr -> 1: Build with Current Configuration
test -s "$TIZENRT_ROOT/build/output/bin/fip.bin"
```

AI 앱을 분리해 올리는 작업은 우선 `loadable_apps` 또는 `loadable_ext_ddr_st7785`를 선택하고, 실제 보드에서 flash/package/앱 로더까지 확인한다.

## `bk7239n`

### 특징

- 저장소의 정확한 보드명은 `bk7239n`이다. `bk2739n`이라는 표기는 사용하지 않는다.
- ARMv8-M Cortex-M33 및 Armino 계열이다.
- `hello`, `loadable_all`, `loadable_apps`, `xip_all` recipe가 제공된다.
- TF-M, secure packaging, signing 단계가 포함될 수 있어 단순 Cortex-M 빌드보다 사전 생성물과 Python 도구 의존성이 크다.

### 추천 시작 recipe

| config | 용도 | 주요 특징 |
| --- | --- | --- |
| `hello` | 기본 bring-up 및 단일 flat 이미지 | `kernel_bk7239n_200204.trpk` |
| `xip_all` | XIP 구성 | NUM_APPS=1, `CONFIG_ARCH_STDARG_H=y` 구성 |

이 저장소의 Mac ARM64 검증에서는 `bk7239n/hello`가 clean build 첫 단계에서 `_otp.h` 및 `os/include/stdarg.h` 오류로 실패했다. 따라서 recipe 선택 시 [AI Build Runbook의 제한 사항](AI_Build_Runbook.md#bk7239nhello의-현재-제한-사항)을 먼저 읽고, 성공 artifact가 실제로 생성되었는지 확인한다.

`loadable_all`과 `loadable_apps`도 저장소에 존재하지만, 이 문서에서는 `hello`와 `xip_all`을 시작 recipe로 사용한다.

## `rtl8721csm`

### 특징

- Realtek AmebaD 기반 ARMv8-M Cortex-M33 계열이다.
- 일반 bring-up, loadable 앱, 테스트, TensorFlow Micro 기반 AI recipe가 함께 제공된다.
- ARM64 이미지에서 `rtl8721csm/hello`는 `kernel_rtl8721csm_200204.trpk`를 생성하는 기본 build smoke test로 검증했다.

### 추천 시작 recipe

| config | 용도 | 주요 특징 |
| --- | --- | --- |
| `hello` | 기본 빌드와 보드 bring-up | TRPK kernel 출력 |
| `loadable_apps` | kernel/app 분리 | loadable 앱 개발 |

AI 개발 후속 recipe로는 `ai_tfmicro`를 사용할 수 있다.

```bash
./dbuild.sh menu
# 5: Clean Build and Re-Configure
# rtl8721csm -> ai_tfmicro -> 1: Build with Current Configuration
```

`ai_tfmicro`는 설정을 선택했다고 inference가 검증되는 것은 아니다. 모델 파일, 예제 앱(`hello_tfmicro` 등), 메모리 크기, 실제 센서 입력과 결과를 보드에서 별도로 검증한다.

`tc`는 테스트 케이스 중심의 별도 recipe다. 시작 recipe를 빌드한 뒤 테스트 설정이 필요할 때 선택한다.

기본 빌드 smoke test:

```bash
./dbuild.sh menu
# 5: Clean Build and Re-Configure
# rtl8721csm -> hello -> 1: Build with Current Configuration
test -s "$TIZENRT_ROOT/build/output/bin/kernel_rtl8721csm_200204.trpk"
```

## `qemu-armv8m`

### 특징

- QEMU `mps2-an505` 머신을 대상으로 하는 Cortex-M33 ARMv8-M 구성이다.
- 실물 보드 없이 빌드, 부팅, TASH 명령, kernel test protocol을 반복 검증할 수 있다.
- 지원 config는 `hello`, `loadable_all`, `loadable_apps`, `xip_all`이다.

### 추천 시작 recipe와 검증

| config | 용도 | 출력/실행 |
| --- | --- | --- |
| `hello` | 가장 빠른 TASH 및 kernel test smoke test | `tinyara`; QEMU runner로 `kernel_tc` |
| `xip_all` | XIP 구성 확인 | `common` 및 XIP package |

`loadable_all`과 `loadable_apps`도 지원 config지만, QEMU 시작 recipe는 `hello`와 `xip_all`로 고정한다. `xip_all`은 `common`과 `app1` 패키지를 함께 확인해야 한다.

QEMU는 BK7239N과의 설정 비교를 위해 SRAM heap을 별도 region으로 둔다.
`hello`는 main RAM 12 MiB와 SSRAM heap 512 KiB를 사용하고, loadable/XIP는
main RAM 4 MiB, loaded-app RAM 8 MiB, SSRAM heap 512 KiB를 사용한다. heap
index는 각각 `0,1`과 `0,2,1`이다. SSRAM 상단 512 KiB는 heap으로 예약하며,
loadable/XIP package는 main RAM 상단의 RAM-backed flash A/B state에 보관한다.

2026-07-26 local evidence에서는 네 config를 각각 clean build한 뒤
LAN9118 network TC와 full Kernel TC까지 확인했다. network TC는 모두
`PASS : 161, FAIL : 0`이고, kernel TC는 `hello`가 `PASS : 459, FAIL : 0`,
`loadable_all`, `loadable_apps`, `xip_all`은 각각
`PASS : 447, FAIL : 0`이었다. 이 결과는 QEMU 소프트웨어 경로의 증거이며
실제 보드 동작은 CI/하드웨어에서 별도로 확인한다.

QEMU `hello`는 PI와 Binary Manager가 모두 꺼져 semaphore holder tracking을
빌드하지 않는다. PI-off `loadable_all`과 `loadable_apps`는 Binary Manager
holder recovery용 preallocated holder 16개를 사용하고, PI-on `xip_all`도
priority inheritance와 Binary Manager용으로 같은 크기를 사용한다. 이
차이는 실제 활성 기능과 동시에 필요한 holder 수에 맞춘 것이다.

실행 명령은 [Mac QEMU ARMv8-M 가이드](Mac_QEMU_ARMv8M_TASH_KernelTC.md)를 사용한다.

```bash
cd "$TIZENRT_ROOT/os"
./dbuild.sh menu
# 5: Clean Build and Re-Configure
# qemu-armv8m -> hello -> 1: Build with Current Configuration

cd "$TIZENRT_ROOT"
python3 .github/scripts/qemu-armv8m-kernel-tc.py \
  --config hello \
  --timeout 1200 \
  --log build/qemu-armv8m/hello-kernel-tc.log \
  --result build/qemu-armv8m/hello-kernel-tc.result.json
```

성공 기준은 새 QEMU 실행의 result JSON `status=pass`와 `Kernel TC End [PASS : n, FAIL : 0]`이다. boot smoke와 full Kernel TC 결과를 구분하고, QEMU 결과는 소프트웨어 경로의 증거로만 사용한다.

## recipe 선택 원칙

1. 각 보드는 위 표의 시작 recipe를 우선 사용한다.
2. 앱 분리, protected, XIP가 목적이면 해당 `loadable_*`/`xip_*` recipe를 선택한다.
3. AI 모델을 올릴 때는 `rtl8721csm`의 시작 recipe를 확인한 뒤 `ai_tfmicro`와 메모리/모델 저장소 설정을 검토한다.
4. build artifact가 생성되어도 보드 실행 및 AI inference가 자동으로 검증된 것은 아니다.
5. 보드명을 추측하지 말고 `build/configs/<board>/`의 실제 디렉터리와 `defconfig`를 기준으로 한다.
