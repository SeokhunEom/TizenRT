# LwIP 사용 방법
**LwIP(경량 IP)**는 임베디드 시스템용으로 설계된 오픈 소스 TCP/IP 스택입니다.
자세한 내용은 [lwip_summary](https://savannah.nongnu.org/projects/lwip/)를 참조하세요.

## 목차
- [주요 특징](#main-features)  
- [사용방법](#how-to-use)

<a id="main-features"></a>
## 주요 기능
**LwIP**는 아래와 같이 여러 네트워크 계층에 걸쳐 tcp/ip 스택에 필요한 다양한 기능을 제공합니다.
- **애플리케이션 레이어** - DNS(도메인 이름 서버), SNMP(단순 네트워크 관리 프로토콜) 에이전트, DHCP(동적 호스트 구성 프로토콜) 서버/client, SNTP(단순 네트워크 시간 프로토콜), 멀티캐스트 DNS, MQTT 클라이언트.
- **전송 계층** - UDP(사용자 데이터그램 프로토콜), UDP-Lite 확장, TCP(전송 제어 프로토콜), RAW 소켓 및 PCB, 버클리형 소켓 API.
- **인터넷 계층** - IPv4 및 IPv6, 다중 네트워크 인터페이스를 통한 패킷 전달, ICMP(인터넷 제어 메시지 프로토콜) 및 ICMPv6, IGMP(인터넷 그룹 관리 프로토콜).
- **링크 레이어** - PPP(지점 간 프로토콜), ARP(이더넷 주소 확인 프로토콜), NDP(Neighbor Discovery 프로토콜) 및 NDPv6.

자세한 내용은 [lwip_document](http://www.nongnu.org/lwip/2_0_x/index.html)를 참조하세요.

### 활성화 방법
/disable 지원 기능을 활성화하려면,
```
cd $TIZENRT_BASEDIR
cd os
./dbuild.sh menuconfig (or make menuconfig)
```
1. LwIP 활성화
	```
	Networking Support -> Networking Stack (LwIP)
	```
2. 기능 선택
	```
	Networking Support -> LwIP options
	```
3. [init.c](../os/net/lwip/src/core/init.c)에 정의된 lwip_init()를 호출하여 LwIP를 초기화합니다.

<br>
<a id="how-to-use"></a>
## 사용방법
TizenRT는 [네트워크 관리자](../docs/HowToPortTizenRTOnWiFiChipset.md)에서 관리하는 기본 네트워크 스택으로 LwIP를 지원합니다.
Network Manager는 네트워크 스택을 캡슐화하므로
공급업체는 LwIP 구성을 고려할 필요가 없습니다.
위에 링크된 가이드를 따르세요.

한 가지 주목할 점은
공급업체는 LwIP에서 *pbuf*라는 네트워크 버퍼를 주의해서 사용해야 합니다.
특히, TizenRT는 벤더가 *struct pbuf*를 직접 할당하는 것을 허용하지 않으며,
하지만 네트워크 장치 구조 내부에는 *tx_buf*를 제공합니다.
또한 TizenRT는 *pbuf*에 정적 메모리 풀을 사용할 것을 권장합니다.
메모리 구성은 [memp.c](../os/net/lwip/src/core/memp.c)에서 구현됩니다.
그러면 공급업체는 메모리 풀을 설정할 수 있습니다.
아래 menuconfig 내부의 기본 구성에 따라:
```
Networking Support -> LwIP options -> Memory Configurations
```

마지막으로 보드와 Wi-Fi 드라이버의 메모리 풀 크기를 고려하면,
공급업체는 LwIP에 사용되는 메시지 상자(*mbox*)의 크기를 조정해야 합니다.
는 .config 파일에서 CONFIG_NET_TCPIP_MBOX_SIZE로 정의됩니다.
이 값은 아래 menuconfig로 설정할 수도 있습니다.
```
Networking Support -> LwIP options -> LWIP Mailbox Configurations -> LWIP Task Mailbox Size
```
