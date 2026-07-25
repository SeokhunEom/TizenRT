# TizenRT 청소 방법

Makefile에는 작성된 모든 파일을 정리하는 두 가지 유형의 명령이 있습니다.  
하나는 ```make clean```이고, 다른 하나는 ```make distclean```입니다. *os* 폴더에서 실행됩니다.

## 깨끗하게 만들기
이 명령은 개체, 라이브러리, .dependent, Make.dep 등과 같은 빌드 절차에서 만들어진 파일을 제거합니다.  
구성이 변경되면 이 명령을 실행하면 처음부터 다시 빌드됩니다.

## make distclean
이 명령에는 내부에 ```make clean```가 포함됩니다. 또한 .config 및 Make.defs와 같은 구성된 파일을 제거합니다.  
새로운 또는 다른 defconfig를 사용하려면 새 defconfig를 구성하기 전에 이 작업을 실행해야 합니다.
