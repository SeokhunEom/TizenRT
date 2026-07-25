# 수동 설정 빌드 환경
이 문서에서는 빌드 환경 설정과 관련된 단계를 설명합니다.

## 목차
- [툴체인 가져오기](#get-the-toolchain)
- [소스 코드 받기](#get-the-source-code)
- [구축 방법](#how-to-build)
- [구성 세트](#configuration-sets)

<a id="get-the-toolchain"></a>
## 툴체인 가져오기

OS별 툴체인을 설치합니다. 지원되는 OS 유형은 "linux"와 "mac"입니다.  
[gcc-arm-none-eabi-6-2017-q1-update-*OS 유형*.tar.bz2](https://developer.arm.com/tools-and-software/open-source-software/developer-tools/gnu-toolchain/gnu-rm/downloads)에서 바이너리와 라이브러리를 가져와서 빌드하세요.
gcc-arm-none-eabi-6-2017-q1-update-*OS Type*.tar.bz2의 압축을 풀고 아래와 같이 경로를 내보냅니다.

```bash
tar xvjf gcc-arm-none-eabi-6-2017-q1-update-[OS Type].tar.bz2
export PATH=<Your Toolchain PATH>:$PATH
```
> **Note**
> 권장 도구 체인은 64비트 컴퓨터에서 완벽하게 작동합니다.

<a id="get-the-source-code"></a>
## 소스 코드 받기

```bash
git clone https://github.com/Samsung/TizenRT.git
cd TizenRT
TIZENRT_BASEDIR="$PWD"
```

<a id="how-to-build"></a>
## 빌드 방법

*$TIZENRT_BASEDIR/os/tools* 디렉터리에서 빌드를 구성합니다.
```bash
cd os/tools
./configure.sh <board>/<configuration_set>
```
구성 파일의 이름은 *defconfig*이며 *build/configs*에 루트가 있는 상대 경로 \<board\>/\<configuration_set\> 아래에 있습니다.  
지원되는 다양한 \<board\>/\<configuration_set\> 조합을 확인하려면 아래를 입력하세요.
```bash
./configure.sh --help
```

위에서 구성한 후 *$TIZENRT_BASEDIR/os*에서 *make menuconfig*를 통해 구성을 수정할 수 있습니다.
```bash
cd ..
make menuconfig
```

*menuconfig*를 사용하려면 [kconfig-프론트엔드 설치](HowtoInstallKconfigFrontend.md)를 참조하세요.

마지막으로 *$TIZENRT_BASEDIR/os*에서 make를 통해 빌드를 시작합니다.
```bash
make
```

빌드된 바이너리는 *$TIZENRT_BASEDIR/build/output/bin*에 있습니다.

빌드된 파일을 정리하려면 [정리 명령](HowtoClean.md)를 참조하세요.  
TizenRT 사용에 대한 문제를 해결하려면 [문제 해결](TroubleShooting.md)를 참조하세요.

<a id="configuration-sets"></a>
## 구성 세트

TizenRT 애플리케이션을 빌드하려면 *build/configs/\<board\>/\<configuration_set\>* 폴더 아래에 *defconfig*라는 기본 구성 파일을 사용합니다.  
특정 구성 설정으로 애플리케이션을 사용자 정의하려면 다음과 같이 *os* 폴더에서 menuconfig 도구를 사용하는 것이 좋습니다.
```bash
make menuconfig
```
우리는 보드 구성에 대해 적극적으로 작업하고 있으며 각 구성 아래의 README 파일에 대한 업데이트를 게시할 예정이라는 점을 명심하십시오.
