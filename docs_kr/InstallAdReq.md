# 추가 요구 사항

아래 기능을 사용하기 위해서는 각 전제조건이 필요합니다.  
configure.sh는 또한 아래와 같이 구성을 빌드하기 위해 누락된 패키지를 표시합니다.
```bash
os$ ./tools/configure.sh artik053/grpc
  Copy build environment files
 
  CAUTION!! To build artik053/grpc, protoc should be installed
  Find details at https://github.com/Samsung/TizenRT/blob/master/external/protobuf/README.md
 
  Configuration is Done!
```
이러한 모든 문서는 [위키 사용자 가이드](https://github.com/Samsung/TizenRT/wiki/Documentations#user-guide)에 나열되어 있습니다.

## gRPC
[gRPC 전제조건](https://github.com/Samsung/TizenRT/blob/master/external/grpc/README.md#pre-requisites)를 참조하세요.

## ROMFS
[ROMFS 사용법](https://github.com/Samsung/TizenRT/blob/master/docs/HowToUseROMFS.md)를 참조하세요.

## OCF
[IoTivity 시작하기](https://iotivity.org/documentation/linux/getting-started)를 참조하세요.

## IoT.js
README, [IoTjs 사용법](https://github.com/Samsung/TizenRT/blob/master/docs/HowToUseIoTjs.md)가 있습니다. STM32F4-NuttX용  
그러나 cmake의 필요성을 보여주지는 않습니다. [STM32F4-NuttX용으로 빌드](https://github.com/Samsung/iotjs/wiki/Build-for-STM32F4-NuttX#linux)를 참조하세요.
