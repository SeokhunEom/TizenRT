# ST Things 기반의 사물을 개발하는 방법 TizenRT

ST Things(SmartThings Things)는 TizenRT 기반의 경량 OCF 개발 프레임워크입니다.  
OCF 디바이스 개발을 위한 API를 제공하고, SmartThings Cloud에 쉽게 연결할 수 있도록 모바일 기기에서 제공하는 SmartThings 서비스를 지원합니다.

## 전제 조건
### TizenRT를 사용하려면
각 세부사항을 참고하세요.  

- Toolchain 설치 [[세부정보]](../README.md#getting-the-toolchain)  
- 보드 관련 사전 요구 사항 [[세부 정보]](../README.md#supported-board--emulator)  
- IoTivity 필수 구성 요소 설치 [[세부 정보]](../external/iotivity/README.md#prerequisites)  
- ROMFS 필수 구성 요소 설치 [[세부 정보]](../tools/fs/README_ROMFS.md#pre-condition)  


### SmartThings 클라우드를 사용하려면
ST Things 기반 TizenRT를 사용하여 개발된 디바이스는 SmartThings Cloud에 연결할 수 있습니다.  
클라우드를 사용하기 위해서는 [SmartThings 개발자](https://smartthings.developer.samsung.com/) 사이트에서 삼성 계정 가입이 필요합니다.
그런 다음 단계를 따르십시오.
1. [마이페이지](https://smartthings.developer.samsung.com/partner/dashboard)에서 MNID(제조업체 ID)를 가져옵니다.  
2. 클라우드 연결 디바이스 [[세부정보]](https://smartthings.developer.samsung.com/docs/devices/smartthings-schema/schema-basics.html)를 만듭니다.
3. 인증서 서명 요청을 생성하고 장치 인증서 [[Details]](https://smartthings.developer.samsung.com/develop/workspace/general-tools/certificate-signing-request.html)에 대한 새 서명 키를 발급합니다.  
4. `certificate.pem` 및 `privateKey.der` 파일을 `$TIZENRT_BASEDIR/tools/fs/contents-romfs/`에 추가합니다.  
5. 다음과 일치하도록 Json 파일을 편집합니다. JSON  
   ```  
    "certificate": "certificate.pem",    
    "privateKey": "privateKey.der"  
   ```  

앱(SmartThings) UI 및 기능을 개발하려면 SmartThings SDK를 사용하세요. [SmartThings SDK](https://smartthings.developer.samsung.com/develop/workspace/general-tools/sdk.html) 사이트를 참고하세요.  
장치 정의는 [[장치 정의]](https://developer.tizen.org/development/iot-extension-sdk/api-guides/things-sdk-api/device-definition)를 제공해야 하는 JSON 구성 파일에 저장됩니다.

## 시작하기
### RT용 Tizen Studio 포함
RT용 Tizen Studio는 Ubuntu에서 애플리케이션을 개발, 빌드, 플래시 및 디버그하는 데 도움이 되는 경량 RTOS(실시간 운영 체제)-based 애플리케이션 개발 환경을 제공하는 IDE입니다. 여기  
OS를 **Ubuntu**로 선택하고 [여기](https://developer.tizen.org/development/tizen-studio/download)에서 RT IDE용 Tizen Studio를 다운로드합니다.  
RT용 Tizen Studio는 v2.0부터 ST Things를 지원합니다.  
[읽어보기](HowToDevelopThingsWithTizenStudioForRT.md)를 참조하세요.

### IDE 없이
ST Things 샘플 앱을 구축하려면 [읽어보기](../apps/examples/st_things/README.md)에서 찾으세요.  

## API 참조
[읽어보기](../docs/API_Reference/README.md)에서 ST Things API를 찾으세요.  
