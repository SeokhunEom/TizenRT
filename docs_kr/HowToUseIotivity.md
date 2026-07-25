# IoTivity 사용법

IoTivity는 사물 인터넷의 새로운 요구 사항을 해결하기 위해 원활한 장치 간 연결을 지원하는 오픈 소스 소프트웨어 프레임워크입니다.  
더 자세한 정보는 [읽어보기](../external/iotivity/iotivity_1.2-rel/README.md) 및 [IoTivity 사이트](https://iotivity.org)에서 확인하세요.  

## IoTivity 기능을 사용하려면

### 1. IoTivity의 빌드 구성 사용

[읽어보기](../external/iotivity/README.md)에서 찾아주세요.  

### 2. 다른 빌드 구성으로

menuconfig를 통해 Iotivity 기능을 설정합니다.  
```
~/$ cd $TIZENRT_BASEDIR
~/TIZENRT_BASEDIR$ cd os
~/TIZENRT_BASEDIR/os$ make menuconfig
```
menuconfig에서 'iotivity 스택 활성화/비활성화'를 선택합니다.  
```
    -+-External Libraries
          |
          +-enable / disable iotivity stack
```

## IoTivity 샘플 앱 빌드 - 간단한 서버

[읽어보기](../apps/examples/iotivity_simpleserver/README.md)에서 찾아주세요.


