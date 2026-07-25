# 포팅 가이드

## 목차
- [정책 및 주의사항](#policy--caution)  
- [커널 및 시스템](#kernel--system)  
- [파일 시스템](#file-system)  
- [회로망](#network)  
- [오디오](#audio)  
- [포팅 확인](#confirmation-of-porting)

## 정책 및 주의사항
- 드라이버 구현 권장 사항  
기본 드라이버 인터페이스 코드(SoC 패키지의 일부)를 플랫폼에 연결하는 대신,  
TizenRT 플랫폼 드라이버 모델에 따라 드라이버를 포함하는 것이 좋습니다.

- 새로운 애플리케이션 포함  
Kconfig 항목을 추가하는 것이 좋습니다. 모든 애플리케이션 구성 및 종속성이 올바르게 포함되어야 합니다.

- 드라이버, 프레임워크 및 시스템 호출 API 사용  
애플리케이션에서 직접 아키텍처 코드를 호출하지 마세요. 보호된 빌드가 활성화되면 차단됩니다.  
애플리케이션은 기본 드라이버와 통신하기 위해 TizenRT 드라이버 인터페이스를 호출해야 합니다.

- TizenRT 코드 구조 유지  
기본 보드 패키지 구조에 맞춰 주변 장치 추가, 새 인터페이스, 새 오픈 소스 코드 등을 수행하는 경우 권장되지 않습니다.  
TizenRT 코드 구조를 이해하고 그 구조를 고수하면 더 나은 아키텍처 설계와 이식성에 도움이 됩니다.

## 커널 및 시스템
1. [새 보드를 추가하는 방법](HowToAddnewBoard.md)
2. [주변기기 사용방법](HowToUsePeripheral.md)
3. [정적 라이브러리를 추가하는 방법](HowToAddStaticLibrary.md)
4. [메모리 구성 방법](HowToConfigureMemory.md)
5. [MPU 사용법](HowToUseMPU.md)
6. [보호된 빌드를 지원하는 방법](HowToSupportTizenRtProtectedBuild.md)
7. [TizenRT 바이너리 헤더를 포팅하는 방법](HowToPortTizenRTBinaryHeader.md)
8. [API 이식할 목록](APIListToBePorted.md)

<a id="file-system"></a>
## 파일 시스템
1. [SmartFS 사용법](HowToUseSmartFS.md)

<a id="network"></a>
## 네트워크
1. [새로운 WiFi 칩셋에 TizenRT를 포팅하는 방법](HowToPortTizenRTOnWiFiChipset.md)
2. [LWIP 사용법](HowToUseLWIP.md)

<a id="audio"></a>
## 오디오
[오디오 사용 방법](HowToUseAudio.md)

<a id="confirmation-of-porting"></a>
## 포팅 확인
1. [네트워크 확인 방법](HowToConfirmNetworkPorting.md)

업데이트 예정
