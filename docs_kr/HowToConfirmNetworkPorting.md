# 네트워크 포팅 확인 방법
TizenRT는 네트워크 성능 측정 도구를 제공합니다. 네트워크 스택의 성공적인 포팅을 지원하는 
LwIP, Wi-Fi 관리자, Wi-Fi 드라이버 등  
.  
안정성과 견고성을 검증하기 위해,
아래 설명된 절차를 반드시 따르십시오.

## 목차
- [Wi-Fi 관리자 테스트](#wi-fi-manager-test)
- [TCP 및 UDP 성능](#tcp-and-udp-performance)

<a id="wi-fi-manager-test"></a>
## Wi-Fi 관리자 테스트
Wi-Fi 관리자 API의 사용 편의성과 성능 평가를 위해 Wi-Fi 관리자는 **wm_test**라는 샘플 애플리케이션을 지원합니다.
TASH 명령을 새로 설치하려면 [TASH를 추가하는 방법](HowToAddTASHCMD.md)를 참조하십시오.  

**wm_test**에서 제공하는 서비스 목록은 다음과 같습니다.
- Wi-Fi 관리자 시작 및 중지
- 네트워크 가입 및 탈퇴
- SoftAP 실행
- 스캔 중
- 연결 정보 가져오기

### **wm_test**를 활성화하는 방법
/disable Wi-Fi 관리자 샘플 애플리케이션을 활성화하려면 다음을 수행합니다.
1. menuconfig 옵션을 사용하여 dbuild.sh 스크립트를 실행합니다.
	```
	cd $TIZENRT_BASEDIR
	cd os
	./dbuild.sh menuconfig (or make menuconfig)
	```
2. 애플리케이션 구성 -> 예제 -> Wi-Fi 관리자 샘플을 선택합니다.

### **wm_test** 사용법
1. 사용 방법을 얻으려면 아래와 같이 wm_test를 입력하십시오.
	```
	TASH>> wm_test
	```
2. Wi-Fi 관리자를 초기화하려면,
	```
	TASH>> wm_test start
	```
3. 기존 네트워크를 연결하려면,
	```
	TASH>> wm_test join <SSID> <SECURITY> <PASSWORD>
	```
	SECURITY 목록은 다음과 같습니다.
	- open
	- wep_shared
	- wpa_aes
	- wpa2_aes
	- wpa2_mixed
> **Note**
> Wi-Fi 관리자는 DHCPC를 자동으로 실행합니다.

4. 현재 연결을 끊으려면,
	```
	TASH>> wm_test leave (or wm_test cancel)
	```
5. 주변 AP를 스캔하려면,
	```
	TASH>> wm_test scan
	```
6. 현재 Wi-Fi 관리자 상태를 확인하려면,
	```
	TASH>> wm_test mode
	```
7. 파일 시스템에 AP 구성을 저장하려면,
	```
	TASH>> wm_test set <SSID> <SECURITY> <PASSWORD>
	```
8. /remove 저장된 AP 구성을 가져오려면,
	```
	TASH>> wm_test get/reset
	```
9. 모든 Wi-Fi Manager API를 테스트하려면,
	```
	TASH>> wm_test auto <SOFTAP_SSID> <SOFTAP_PASSWORD> <SSID> <SECURITY> <PASSWORD>
	```
10. Wi-Fi 관리자 통계를 얻으려면,
	```
	TASH>> wm_test stats
	```
11. Wi-Fi 관리자 초기화를 해제하려면,
	```
	TASH>> wm_test stop
	```

### 스트레스 테스트
**wm_test**는 [stress_tool](../external/stress_tool/)에 정의된 스트레스 테스트 도구를 활용합니다. 반복적 노화 테스트 모듈을 제공하는
.  
스트레스 테스트 도구를 활성화하려면 다음을 수행하십시오.
1. menuconfig 옵션을 사용하여 dbuild.sh 스크립트를 실행합니다.
	```
	cd $TIZENRT_BASEDIR
	cd os
	./dbuild.sh menuconfig (or make menuconfig)
	```
2. 외부 라이브러리 선택 -> 스트레스 도구 활성화.
3. 애플리케이션 구성 선택 -> WiFi 관리자용 스트레스 도구 활성화.
4. 스트레스 테스트를 실행하려면 아래와 같이 입력하세요.
마지막으로 스트레스 테스트를 실행할 수 있습니다.
	```
	TASH>> wm_test stress
	```
> **Note**
> 위의 2번과 3번 구성을 입력하려면 필요할 때마다 '도움말'을 참조하세요.
### Wi-Fi 관리자 UTC
Wi-Fi 관리자 UTC를 활성화하려면 다음을 수행하십시오.
1. menuconfig 옵션을 사용하여 dbuild.sh 스크립트를 실행합니다.
	```
	cd $TIZENRT_BASEDIR
	cd os
	./dbuild.sh menuconfig (or make menuconfig)
	```
2. 애플리케이션 구성 -> 예제 -> 테스트 케이스 예제를 선택합니다.
3. TestCase 예 선택 -> Wi-Fi Manager UTC TestCase 예.
4. Wi-Fi 관리자 UTC를 조정하려면 아래와 같이 입력하세요.
	```
	TASH>> wifi_manager_utc
	```
> **Note**
> 위의 2번과 3번 구성을 입력하려면 필요할 때마다 '도움말'을 참조하세요.

모든 UTC가 성공적으로 통과되면 결과는 아래와 같습니다.
```
############ WiFiManager UTC End [PASS : 25, FAIL : 0] ##############
```

<a id="tcp-and-udp-performance"></a>
## TCP 및 UDP 성능
Iperf3은 IP 네트워크에서 달성 가능한 최대 대역폭을 능동적으로 측정하기 위한 도구입니다.
타이밍, 버퍼 및 프로토콜과 관련된 다양한 매개변수의 조정을 지원합니다.  
자세한 내용은 [아이퍼프](https://iperf.fr)를 참조하세요.

### iperf 사용법
시스템에 성공적인 LwIP 로딩이 있다고 가정하면 iperf3을 사용하여 TCP/UDP 처리량을 측정할 수 있습니다.
iperf를 활성화하려면 다음을 수행하십시오.
1. menuconfig 옵션을 사용하여 dbuild.sh 스크립트를 실행합니다.
	```
	cd $TIZENRT_BASEDIR
	cd os
	./dbuild.sh menuconfig (or make menuconfig)
	```
2. 외부 라이브러리 -> cJSON 라이브러리를 선택합니다.
3. 애플리케이션 구성 -> 시스템 라이브러리 및 추가 기능 -> iperf 앱을 선택합니다.
4. Wi-Fi 연결에 성공한 후 다음을 입력하세요. 보드의
	* iperf 서버 보드의
		```
		TASH>> iperf -s
		```
	* iperf 클라이언트
		```
		TCP: TASH>> iperf -c <SERVER_IPADDR> -t <TIME>
		UDP: TASH>> iperf -c <SERVER_IPADDR> -u -t <TIME>
		```
