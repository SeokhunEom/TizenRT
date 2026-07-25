# IoT.js 사용 방법

IoT.js는 JavaScript를 사용하는 사물 인터넷용 오픈 소스 소프트웨어 플랫폼입니다.
자세한 내용은 소스에서 http://www.iotjs.net 또는 배송된 [읽어보기](..//external/iotjs/README.md)를 참조하세요.

## 목차
- [참조 구성 사용](#use-reference-configuration)
- [IoT.js 기능을 활성화하려면](#to-enable-iot.js-feature)
- [스크립트를 로드하는 방법](#how-to-load-scripts)
- [네트워크 구성 방법](#how-to-configure-network)
- [자원](#resources)

<a id="use-reference-configuration"></a>
## 참조 구성 사용

    machine=artik053 # 지원되는 대상에 맞게 조정  
    cd os && ./tools/configure.sh ${machine}/iotjs # Generate os/.config  
    make menuconfig # Verify WiFi credentials  
    make # Will build os image with IoT.js in and example  
  

## IoT.js 기능을 활성화하려면

필요한 기능을 선택하려면 아래 예와 같이 menuconfig를 사용할 수 있습니다.

    make menuconfig  
    # 애플리케이션 구성 --->  
    #  예 --->  
    #   [*] IoT.js 시작 예  
    #     (/rom/example/index.js) 메인 자바스크립트 파일  
    #     [ ] WiFi 연결  
    #     (0) 인증 유형  
    #     (0) 암호화 유형  
    # 애플리케이션 진입점(...) --->  
    #   (X) IoT.js 시작 예  


<a id="how-to-load-scripts"></a>
## 스크립트 로드 방법

파일을 다른 위치에 로드할 수 있습니다. 편의상 에서 설명한 대로 ROM 파티션을 추가하는 접근 방식을 사용하는 것이 좋습니다.
[HowToUseROMFS.md](HowToUseROMFS.md). 그런 다음 다음과 같이 tools/fs/contents-romfs에 스크립트를 배치합니다.

    고양이 도구/fs/contents-romfs/example/index.js 
    console.log(JSON.stringify(프로세스));


<a id="how-to-configure-network"></a>
## 네트워크 구성 방법

스크립트를 실행하기 전에 네트워크에 연결하려는 경우 부팅 시 WiFi를 활성화할 수도 있습니다. "make menuconfig"를 다시 실행하고 자격 증명을 편집해야 합니다.

    make menuconfig  
    # 애플리케이션 구성 --->  
    #  예 --->  
    #   [*] IoT.js 시작 예  
    #     (/rom/example/index.js) 메인 자바스크립트 파일  
    #     [*] WiFi 연결
    #     ("APSSID") AP의 SSID
    #     ("APPassword") AP의 암호    
    #     (4) 인증 유형  
    #     (4) 암호화 유형  
    # 애플리케이션 진입점(...) --->  
    #   (X) IoT.js 시작 예  

WiFi SSID와 비밀번호로 "AP의 SSID"와 "AP의 암호"를 편집하세요.
"인증 유형" 및 "암호화 유형" 값의 경우 <Help> 메뉴를 사용하여 일치하는 값을 찾습니다.
WiFi 구성. 예를 들어 인증 유형이 "WPA 및 WPA2 PSK"인 경우 4이고 "TKIP"인 암호화 유형인 경우 4입니다.


<a id="resources"></a>
## 리소스

* https://github.com/Samsung/iotjs/wiki/Build-for-ARTIK053-TizenRT
* https://archive.fosdem.org/2018/schedule/event/tizen_rt/
* https://www.slideshare.net/SamsungOSG/tizen-rt-a-lightweight-rtos-platform-for-lowend-iot-devices
* https://source.tizen.org/documentation/tizen-rt/tizen-rt-long-term-goals
