# STDK 기반 TizenRT를 이용하여 물건을 개발하는 방법   
STDK(Smart Things Development Kit)는 Smart Things 생태계의 기능 중 하나입니다. 이 가이드에서는 TizenRT에서 STDK를 활성화하고 이를 사용하여 애플리케이션을 개발하는 방법을 예를 들어 설명합니다.

깜박이는 Led의 일반적인 예를 선택하여 소위 LED 사물을 개발하고 SmartThings 모바일 앱을 통해 이를 제어하는 ​​방법을 보여줍니다.

## 목차
* [전제조건](#prerequisites)
* [TizenRT 소스 가져오기](#get-tizenrt-source)
* [장치 ID 만들기](#create-device-identity)
* [SmartThings Devworkspace에 장치 프로필 등록](#register-device-profile-on-smartthings-devworkspace)
* [빌드 및 플래시](#build-and-flash)
* [EasySetup 및 Control을 시도해 보세요.](#try-to-do-easysetup-and-control)
* [STDK 장치를 개발하는 방법](#how-to-develop-your-stdk-device)

<a id="prerequisites"></a>
## 전제 조건   
이 섹션에서는 ESP-WROVER-KIT 보드용 STDK를 설치하고 빌드하기 위한 사전 요구 사항 단계를 설명합니다.   

* 보드 관련 사전 요구 사항 [[세부 정보]](../README.md#supported-board--emulator).   
* Toolchain 설치 [[세부정보]](https://smartthings.developer.samsung.com/docs/devices/direct-connected-devices/setup-environment.html).   

<a id="get-tizenrt-source"></a>
### TizenRT 소스 가져오기   
#### Clone TizenRT 소스 코드입니다.   
```bash
git clone https://github.com/Samsung/TizenRT.git
cd TizenRT
TIZENRT_BASEDIR="$PWD"
```
#### STDK용 Init 하위 모듈입니다.   
+ 업데이트 하위 모듈   
```bash
git submodule update --init "external/stdk/st-device-sdk-c"
git submodule update --init "external/libsodium/libsodium"
```

<a id="create-device-identity"></a>
## 장치 ID 생성   
SmartThings 클라우드에 연결하려면 IoT 장치에 인증이 필요합니다. 이러한 데이터는 일반적으로 파일 형식으로 존재합니다. 이러한 인증 데이터를 생성하려면 [개발자 작업공간](https://smartthings.developer.samsung.com/)의 개인정보 창에서 MNID를 알아야 합니다. 삼성 계정으로 로그인하신 후 MNID를 먼저 확인해 주세요.   

터미널 창을 열고 다음 stdk-keygen 명령을 실행하여 장치 ID를 만듭니다.   

```bash
$ cd external/stdk/st-device-sdk-c/tools/keygen/linux
$ ./stdk-keygen -h
usage: stdk-keygen -m MNID [-f firmware_version]

$ ./stdk-keygen -m **** -f V201910       # replace '****' with your MNID
Go Device Identity of Developer Workspace.

Serial Number:
┌──────────┬──────────┐
│ STDK**** │ ce**2**3 │
└──────────┴──────────┘

Public Key:
1D********a21F********8WwP********yU/n8vFvM=

$ tree
.
├── ed25519.pubkey
├── ed25519.seckey
└── output_STDK****ce**2**3
    └── device_info.json
```
컴퓨터에서 명령을 실행한 후 일련번호와 공개 키를 복사하세요. [개발자 작업공간](https://smartthings.developer.samsung.com/)를 통해 해당 값을 SmartThings 클라우드에 업로드해야 합니다.

위와 같은 옵션이 포함된 명령으로 디바이스 ID를 생성한 후 바로 사용할 수 있는 device_info.json 파일을 직접 얻을 수 있습니다.   

<a id="register-device-profile-on-smartthings-devworkspace"></a>
## SmartThings Devworkspace에 장치 프로필 등록   
### [개발작업공간](https://smartthings.developer.samsung.com/)에 로그인합니다.   
페이지 오른쪽 상단에서 '로그인'을 선택하세요.   
![로그인을 선택하세요](../docs/media/STDK_SignIn1.png)   
![로그인을 선택하세요](../docs/media/STDK_SignIn2.png)   
### SmartThings 클라우드에서 프로젝트를 생성합니다.   
SmartThings Cloud에 새로운 프로젝트를 등록해야 합니다.   

1. 프로젝트를 생성합니다.   
    1. 새 프로젝트(또는 장치)를 등록하려면 '새 프로젝트'를 선택하세요.   
![새 프로젝트 선택](../docs/media/STDK_Create_New_Project.png)   
    1. '장치 통합'을 선택합니다.   
!['장치 통합'을 선택하세요.](../docs/media/STDK_Selection_Device_Integration.png)   
    1. '직접 연결' 유형을 선택하세요.   
![직접 연결 선택](../docs/media/STDK_Selection_Direct_Connected.png)   
    1. 장치 이름을 입력하고 'CREATE PROJECT'를 선택합니다.   
![장치 이름을 입력하고 'CREATE PROJECT'를 선택합니다.](../docs/media/STDK_Create_Project_Direct_Connected.png)   
1. 장치 프로필을 생성합니다.   
장치 프로필에는 구성 요소, 기능 및 메타데이터(ID, 이름 등)가 포함됩니다. 이러한 정보는 IoT 장치가 수행할 수 있는 작업과 속성을 정의합니다. "장치 프로필로 이동"을 클릭한 다음 나머지 필수 정보를 모두 입력하세요.   

    1. '장치 프로필 추가'를 선택합니다.   
!['장치 프로필로 이동'을 선택하세요.](../docs/media/STDK_Add_A_Device_Profile_1.png)  
!['장치 프로필 추가'를 선택하세요.](../docs/media/STDK_Add_A_Device_Profile_2.png)   
    장치 프로필에는 구성 요소, 기능 및 메타데이터(ID, 이름 등)가 포함됩니다. 이 정보는 IoT 장치가 수행할 수 있는 작업과 속성을 정의합니다.   
!['장치 프로필 추가'를 선택하세요.](../docs/media/STDK_Add_A_Device_Profile_2.png)   
        - 새 프로필을 생성하여 프로젝트에 추가하세요.   
        장치 속성을 입력하고 '다음'을 선택하세요.   
![새로 만들기를 선택하세요.](../docs/media/STDK_create_a_new_profile_2.png)   
        - 장치에 대한 호환성을 추가합니다.   
        '기능 추가'를 선택합니다. 그리고 스위치, 스위치 레벨, 색상 제어, 상태 확인과 같은 기능을 추가하세요. 
![새로 만들기를 선택하세요.](../docs/media/STDK_Add_Copability_1.png)   
        이때 장치의 연결 상태를 업데이트하려면 "상태 확인" 기능을 추가해야 합니다. 이는 주 구성 요소에만 필요합니다.   
![새로 만들기를 선택하세요.](../docs/media/STDK_Add_Copability_2.png)   
        - UI 표시를 선택합니다.   
        기본 상태 및 동작을 선택합니다.   
![새로 만들기를 선택하세요.](../docs/media/STDK_Select_UI_Display.png)   
    1. '장치 온보딩 추가'를 선택합니다.  
    IoT 디바이스와 SmartThings Cloud 간의 초기 연결 프로세스를 지원하기 위한 정보를 정의합니다.   
    "장치 온보딩으로 이동"을 클릭한 다음 나머지 필수 정보를 모두 입력하세요.   
![장치 온보딩 추가](../docs/media/STDK_Add_Device_Onboarding_1.png)   
![장치 온보딩 추가](../docs/media/STDK_Add_Device_Onboarding_2.png)   
![장치 온보딩 추가](../docs/media/STDK_Add_Device_Onboarding_3.png)   
    1. '테스트할 장치 프로필 배포'를 선택합니다.  
    테스트를 위해 장치를 SmartThings 플랫폼에 게시할 수 있습니다. 그런 다음 SmartThings 앱을 통해 장치에 액세스할 수 있습니다. 실제로 이 단계는 자체 테스트를 위한 것입니다. 등록된 조직 ID(예: 회사 MNID)로 장치를 공식적으로 게시하려면 아래 프로세스를 참조하세요.   
![장치 온보딩 추가](../docs/media/STDK_Deploy_1.png)   
    1. 장치 ID를 등록합니다.
    이 단계에서는 장치 ID 만들기 단계에서 생성된 장치 ID 데이터를 업로드합니다.
![장치 온보딩 추가](../docs/media/STDK_Deploy_2.png)   
        나타나는 공개 키 값 상자에 키를 복사한 공개 키를 붙여넣습니다. 그런 다음 추가 버튼을 클릭하십시오.
![장치 온보딩 추가](../docs/media/STDK_Input_Public_Key.png)   

<a id="build-and-flash"></a>
### 빌드 및 플래시   
#### 샘플 애플리케이션 폴더에 장치 정보를 입력합니다.   
1. 온보딩 데이터를 다운로드하세요.   
"onboarding_config.json" 파일을 "[TizenRT]/apps/examples/stdk_smart_lamp/"로 다운로드합니다.   
![장치 온보딩 추가](../docs/media/STDK_Download_onboarding_config.png)   
1. 장치 ID 파일을 응용 프로그램 폴더에 복사합니다.   
```bash   
// in [TizenRT]/external/stdk/st-device-sdk-c/tools/keygen/linux
$ cd external/stdk/st-device-sdk-c/tools/keygen/linux

$ cat output_STDK****ce**2**3/device_info.json   
{
  "deviceInfo": {
    "firmwareVersion": "V201910",
    "privateKey": "3d********AOyY********ezJQ********TMKLGxzbQ=",
    "publicKey": "1D********a21F********8WwP********yU/n8vFvM=",
    "serialNumber": "STDK****ce**2**3"
  }
}
// Copy device info file under "[TizenRT]/apps/examples/stdk_smart_lamp/apps/examples/stdk_smart_lamp/".
$ cp ./output_STDK****ce**2**3/device_info.json ../../../../../../apps/examples/stdk_smart_lamp/apps/examples/stdk_smart_lamp/   
```

#### 구성 및 빌드를 설정합니다.   
스마트 LED 애플리케이션에 포함된 'esp_wrover_kit/stdk' 구성을 사용하겠습니다. "./dbuild 메뉴" 명령을 통해 설정할 수 있습니다.   
아래 단계를 따르십시오.

1. 구성
```bash
// In [TizenRT]/os
$ ./dbuild.sh menu
======================================================
  "Select Board"
======================================================
  ...
  "6. esp_wrover_kit"
  ...
  "x. EXIT"
======================================================
6               // Select 'esp_wrover_kit'.

esp_wrover_kit is selected
==================================================
  "Select Configuration of esp_wrover_kit"
==================================================
  ...
  "6. STDK"
  ...
==================================================
6               // Select "STDK".

STDK is selected
  Copy build environment files
 
  CAUTION!! To download esp_wrover_kit/STDK built image, genromfs should be installed
  Find details at https://github.com/Samsung/TizenRT/blob/master/docs/HowToUseROMFS.md
 
  Configuration is Done!
======================================================
  "Select build Option"
======================================================
  "1. Build with Current Configurations"
  "2. Re-configure"
  "3. Menuconfig"
  "4. Build Clean"
  "5. Build Dist-Clean"
  "6. Build SmartFS Image"
  "x. Exit"
======================================================
1               // Select "Build with Current Configurations"

...
######################################
##          Library Sizes           ##
######################################
    .data   .bss    .text    Total
    356     12560   222108  235024  libexternal.a
    340     8544    118702  127586  libnet80211.a
...
    0   1036    2164    3200    NOLIB
    24  380     2076    2480    libmm.a
    4   4   1904    1912    librtc.a
    0   24  980     1004    libwque.a
    0   42  800     842     libcore.a
    0   0   796     796     libboard.a
    0   0   102     102     libsoc.a
    4   1   0   5   libwpa2.a
    0   1   0   1   libwps.a
======================================================
  "Select build Option"
======================================================
  "1. Build with Current Configurations"
  "2. Re-configure"
  "3. Menuconfig"
  "4. Build Clean"
  "5. Build Dist-Clean"
  "6. Build SmartFS Image"
  "d. Download"
  "x. Exit"
======================================================
d               // Select "Download"
==================================================
  "Select download option"
==================================================
  "1. ALL"
  "2. OS"
  "4. BOOTLOADER"
  "5. ROMFS"
  "6. ERASE_ALL"
  "u. USBrule"
  "x. Exit"
==================================================
1               // Select "All"
 ALL
make -C ../build/tools/esp32  ALL
...
Leaving...
Hard resetting via RTS pin...
make[1]: Leaving directory '/home/tizenrt/ws/public/priv/TizenRT/build/tools/esp32'

                // DONE
```
이제 장치 온보딩 준비가 완료되었습니다.

<a id="try-to-do-easysetup-and-control"></a>
### EasySetup 및 Control을 시도해 보세요.   
Google Play 앱 스토어를 통해 [SmartThings 애플리케이션](https://play.google.com/store/apps/details?id=com.samsung.android.oneconnect)를 다운로드할 수 있습니다.   

easysetup을 통해 모바일과 장치를 연결하기 전에 앱 설정을 통해 개발자 모드를 활성화해야 합니다.   
* [개발자 모드를 활성화하는 방법](https://smartthings.developer.samsung.com/docs/testing/developer-mode.html).   

easysetup을 시도해 보세요.   
![STDK 사물 장치 개발을 위한 워크플로](../docs/media/STDK_EasySetup.png)  
이제 SmartThings 모바일 애플리케이션을 통해 스마트 LED 장치(샘플)를 제어할 수 있습니다.

<a id="how-to-develop-your-stdk-device"></a>
## STDK 장치 개발 방법   
['시작하기'](https://github.com/SmartThingsCommunity/st-device-sdk-c-ref/blob/master/doc/getting_started.md)에서 더 자세한 빌드 접근 방식을 볼 수 있습니다.