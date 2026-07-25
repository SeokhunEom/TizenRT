# AI 빌드 런북

이 문서는 다른 AI 에이전트가 TizenRT 빌드를 같은 순서로 재현하도록 만든 실행 절차이다. 목표는 `os/dbuild.sh`의 대화형 흐름을 따르되, 필요하면 같은 상태 전이를 명령행으로 재현하는 것이다.

## 기준 위치

```bash
cd /Users/seokhun/Projects/TizenRT/master/os
```

이 문서의 명령은 모두 `os/`에서 실행한다. 저장소 루트에서 실행하면 `./dbuild.sh`를 찾지 못하거나 상대 경로가 달라진다.

## 사전 확인

```bash
which docker
docker images --format '{{.Repository}}:{{.Tag}}' | grep 'tizenrt'
test -f .config && grep -E 'CONFIG_ARCH_BOARD=|CONFIG_DOCKER_VERSION=|CONFIG_APP_BINARY_SEPARATION=' .config
```

- `dbuild.sh`는 Docker가 없으면 즉시 종료한다.
- Docker image tag는 기본값이 `1.5.8`이지만, 현재 `.config` 또는 선택한 `defconfig`에 `CONFIG_DOCKER_VERSION`이 있으면 그 값을 따른다.
- macOS Docker Desktop에서 pull이 실패하고 Keychain 관련 오류가 나오면 실제 이미지 문제가 아니라 Docker credential helper가 macOS login Keychain을 읽지 못하는 경우가 있다. 이때는 일반 터미널에서 `security -v unlock-keychain ~/Library/Keychains/login.keychain-db`를 먼저 실행한 뒤 다시 `./dbuild.sh menu`를 시도한다.

## 대화형 빌드 절차

```bash
./dbuild.sh menu
```

이미 `.config`가 있으면 먼저 build option 메뉴가 나온다.

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

기존 설정을 버리고 새 `defconfig`로 전체 clean 후 빌드하려면 다음 순서로 입력한다.

1. build option에서 `5`를 선택한다.
2. board 목록에서 대상 board를 선택한다.
3. configuration 목록에서 대상 config를 선택한다.
4. 다시 build option 메뉴가 나오면 `1`을 선택한다.

설정이 없으면 처음부터 board/config 메뉴가 나온다. 이 경우에는 board, config를 고른 뒤 build option에서 `1`을 선택한다.

## 명령행 재현 절차

대화형 입력을 자동화하려면 다음 형식을 사용한다.

```bash
./dbuild.sh distclean configure <board> <config> build
```

`loadable_apps` 빌드 흐름을 확인할 때 사용한 재현 명령은 다음과 같다.

```bash
./dbuild.sh distclean configure rtl8730e loadable_apps build
```

이 명령은 대화형 메뉴에서 `5 -> rtl8730e -> loadable_apps -> 1`을 고르는 것과 같은 상태 전이를 만든다.

## 상태 전이

`dbuild.sh`는 내부적으로 다음 상태를 사용한다.

| 상태 | 의미 | 다음 단계 |
| --- | --- | --- |
| `NOT_CONFIGURED` | `os/.config`가 없다. | board/config 선택 |
| `BOARD_CONFIGURED` | board만 선택됐다. | config 선택 |
| `CONFIGURED` | `.config`는 있으나 최종 binary가 없다. | build option 선택 |
| `BUILT` | `build/output/bin`에 board별 kernel binary가 있다. | build, clean, download 선택 |
| `PREPARE_DL` | download option을 골라야 한다. | download option 선택 |
| `DOWNLOAD_READY` | download 인자가 준비됐다. | `make download` 실행 |

중요한 점은 `distclean`이 끝나면 상태가 다시 `NOT_CONFIGURED`로 돌아간다는 것이다. 그래서 전체 clean 후에는 반드시 board/config를 다시 고른다.

## 내부에서 실행되는 일

1. `dbuild.sh`가 Docker image를 확인하고 없으면 pull한다.
2. `configure` 단계에서 `os/tools/configure.sh <board>/<config>`가 실행된다.
3. `configure.sh`는 `build/configs/<board>/<config>/defconfig`를 `os/.config`로 복사하고, `build/configs/<board>/Make.defs`를 `os/Make.defs`로 복사한다.
4. `build` 단계에서 `docker run ... -v <repo>:/root/tizenrt -w /root/tizenrt/os ... make`가 실행된다.
5. `make` 출력은 `os/build.log`에도 저장된다.
6. 최종 산출물은 `build/output/bin/` 아래에 생성된다.

## 빌드 후 확인

```bash
grep -E 'CONFIG_ARCH_BOARD=|CONFIG_BUILD_PROTECTED|CONFIG_APP_BINARY_SEPARATION|CONFIG_SUPPORT_COMMON_BINARY|CONFIG_XIP_ELF|CONFIG_APP[12]_BIN_NAME' .config
tail -80 build.log
find ../build/output/bin -maxdepth 2 -type f | sort
test -f .bininfo && cat .bininfo
```

`rtl8730e/loadable_apps` 기준으로 중요한 설정은 다음과 같다.

```text
CONFIG_BUILD_PROTECTED=y
CONFIG_APP_BINARY_SEPARATION=y
CONFIG_SUPPORT_COMMON_BINARY=y
CONFIG_COMMON_BINARY_NAME="common"
CONFIG_XIP_ELF=y
CONFIG_APP1_INFO=y
CONFIG_APP1_BIN_NAME="app1"
# CONFIG_APP2_INFO is not set
```

`build/output/bin`에는 board 설정과 후처리 결과에 따라 `kernel_*`, `app1_*`, `common_*`, `user/` 하위 binary, debug copy가 생긴다. 정확한 최종 이름은 `os/.bininfo`를 기준으로 확인한다.

## 검증된 실행 예

다음 명령은 이 문서를 작성하면서 실제로 실행했고 종료 코드 0으로 끝났다.

```bash
./dbuild.sh distclean configure rtl8730e loadable_apps build
```

확인된 핵심 결과는 다음이다.

```text
KERNEL_BIN_NAME=kernel_rtl8730e_200204.trpk
APP1_BIN_NAME=app1_rtl8730e_190412.trpk
COMMON_BIN_NAME=common_rtl8730e_200204.trpk
```

`build.log`의 마지막 검증 단계에서는 `KERNEL`, `APP1`, `COMMON` size verification이 모두 `PASS`였고, common, kernel, app package header verification도 모두 `SUCCESS`였다.

## 실패 시 우선순위

1. Docker pull 실패면 Docker Desktop, credential helper, Keychain 상태를 먼저 본다.
2. `configure.sh`가 실패하면 `<board>/<config>` 이름과 `build/configs/<board>/<config>/defconfig` 존재 여부를 확인한다.
3. 빌드 중 link 실패면 `os/build.log`에서 첫 번째 `error:` 또는 `Undefined Symbols`를 찾는다.
4. link 후 실패면 `os/tools/validate_output.py`, `check_package_size.py`, board별 `*_make_bin.sh`와 partition 설정을 확인한다.
5. 생성된 `os/.config`, `os/Make.defs`, `include/tinyara/config.h`, `os/.bininfo`는 빌드 산출물이다. 원본 설정을 바꿔야 하면 `build/configs/<board>/<config>/defconfig`와 `build/configs/<board>/Make.defs`를 검토한다.
