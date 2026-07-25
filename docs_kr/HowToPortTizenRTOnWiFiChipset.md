# 새로운 Wi-Fi 칩셋에 TizenRT를 포팅하는 방법
TizenRT는 TizenRT의 네트워크 스택에서 공급업체의 네트워크 드라이버를 인터페이스하는 일반 네트워크 관리 아키텍처를 갖추고 있습니다.
는 이더넷, Wi-Fi, 블루투스 등을 대상으로 합니다.
이를 위해 우리는 __Network Manager__,
TizenRT 커널의 모든 네트워크 스택과 드라이버를 관리하는 원핸드 솔루션,
는 공급업체가 드라이버를 쉽게 포팅할 수 있는 환경을 제공합니다.

새로 추가된 칩셋에 TizenRT의 Network Manager를 인터페이스하는 방법에 대한 지침을 제공합니다.
TizenRT의 코드 트리에 Wi-Fi 칩셋 드라이버를 통합하기 위해,
및 공급업체의 드라이버를 TizenRT의 네트워크 스택에 연결하는 방법,
및 상위 추상화 계층에서 Wi-Fi 구성을 가져오고 설정합니다.

또한 새로운 Wi-Fi 칩셋 라이브러리와 해당 Wi-Fi 드라이버 코드를 포팅하는 방법을 소개합니다.
특히, TizenRT를 통해 새로운 Wi-Fi 칩셋을 포팅하는 방향으로,
공급업체는 다음 중 하나를 결정할 수 있습니다.  
1) 독점 신청자 코드베이스를 사용하거나  
2) TizenRT의 Wi-Fi 솔루션을 직접 사용하려면,  
및 코드 세트는 정적 라이브러리(_*.a_)에서 열거나 제공할 수 있습니다. 공급업체의 결정에 따라
.

## 목차 네트워크 관리자에 대한
- [네트워크 관리자에 대한 인터페이스](#interface-to-network-manager)
- [외부 Wi-Fi 라이브러리를 추가하는 방법](#how-to-add-external-wi-fi-library)
- [WPA_SUPPLICANT 사용법](#how-to-use-wpa_supplicant)
- [Wi-Fi 칩셋 드라이버 통합](#incorporate-wi-fi-chipset-driver)

<a id="interface-to-network-manager"></a>
## 네트워크 관리자에 대한 인터페이스
이 섹션에서는
우리는 공급업체별 네트워크 관리자를 구성하는 방법을 제공합니다.
는 네트워크 스택 리소스를 활용하고 네트워크 스택에 연결된 데이터 흐름을 제어합니다.

게다가 __Wi-Fi Manager__를 소개합니다.
Wi-Fi 연결 제어를 위한 관리 프레임워크 사용자 도메인 상위 계층에서
,
및 경량 넷링크 802.11(*lwnl80211*)을 정의하는 방법 제공
는 각각 커널과 사용자 공간에서 Network Manager와 Wi-Fi Manager 사이의 드라이버 인터페이스입니다.

### 네트워크 스택 불러오기
네트워크 관리자는 자체 네트워크 스택을 캡슐화합니다.
따라서 공급업체와 /or 사용자는 네트워크 스택에 직접 액세스하거나 설정할 필요가 없습니다.
대신 Network Manager는 1) 네트워크 스택을 구성하고
2) 공급업체의 Wi-Fi 드라이버 인터페이스와 연결합니다.
아래 단계를 따르십시오.

#### 1. 네트워크 관리자 초기화
*CONFIG_NET*가 활성화되면,
OS가 시작되면 Network Manager가 자동으로 시작됩니다. 네트워크 스택을 포함한
, **경량 IP(LwIP)**, 기본적으로.
CONFIG_NET_LOOPBACK_INTERFACE가 설정된 경우
그러면 *lo*라는 루프백 인터페이스가 *127.0.0.1*의 IP 주소로 초기화됩니다.
를 사용할 준비가 되었습니다.
따라서 공급업체는 다른 네트워크 스택을 초기화할 필요가 없습니다.
및 자체 루프백 인터페이스를 새로 등록합니다.
대신 공급업체는 네트워크 장치 구조(*netdev*)를 통해서만 TizenRT의 네트워크 스택을 활용할 수 있습니다.

<a id="2-network-manager-configuration"></a>
#### 2. 네트워크 관리자 구성
공급업체는 TizenRT의 네트워크 관리자에 네트워크 장치(*netdev*) 구조를 등록해야 하며, 인터페이스 이름, 유형, 연산, 전송 버퍼, 공급업체별 구조 등으로 구성된

네트워크 스택에서 인터페이스를 여는 데 필요한 netdev_config 구조를 작성하려면,
아래 [netdev_mgr.h](../os/include/tinyara/netmgr/netdev_mgr.h)를 참조하세요.

```
	struct netdev_config {
		struct nic_io_ops *ops;
		int flag;
		int mtu;
		int hwaddr_len;
		uint8_t hwaddr[NM_MAX_HWADDR_LEN];

		int is_default;
		union {
			struct ethernet_ops *eth;
			struct trwifi_ops *wl;
		} t_ops;
		netdev_type type;

		int (*d_ioctl)(struct netdev *dev, int cmd, unsigned long arg); // SIOCSMIIREG
		void *priv;
	};

```
- *struct nic_io_ops ops*: *linkoutput* 및 *igmp_mac_filter*의 드라이버별 함수 포인터 구조입니다. 전자는 공급업체별 링크 출력 작업에 사용되고 후자는 선택적으로 IGMP MAC 필터로 설정되어 MAC 수준에서 IGMP 메시지를 허용합니다. 그런 다음 이 작업은 패킷 전송 및 IGMP 세부 정보에 사용하기 위해 네트워크 스택에 등록됩니다.
- *flag*: [netdev_mgr.h](../os/include/tinyara/netmgr/netdev_mgr.h)에 정의된 NM_FLAG_BROADCAST, NM_FLAG_ETHARP, NM_FLAG_ETHERNET, NM_FLAG_IGMP 및 NM_FLAG_MLD6와 같은 인터페이스 구성 플래그입니다.
- *mtu*: 공급업체 드라이버에서 지원하는 MTU 크기입니다.
- *hwaddr_len*: MAC 주소 바이트 길이(일반적으로 6바이트).
- *is_default*: 네트워크 스택의 기본 인터페이스 사양입니다.
- *t_ops*: [lwnl80211](#light-weight-netlink-80211-driver)에 연결된 제어 도메인 공급업체별 드라이버 작업 집합입니다.
- *netdev_type 유형*: NM_LOOPBACK, NM_WIFI, NM_ETHERNET 또는 NM_UNKNOWN.
- *d_ioctl*: PHY 계층과 통신하기 위한 공급업체별 ioctl 명령(SIOCSMIIREG에서 호출).
- *priv*: 공급업체별 네트워크 장치 구조

#### 3. 네트워크 장치 등록
Wi-Fi 드라이버는 아래와 같이 netdev 등록 기능을 호출해야 합니다. 드라이버를 사용할 준비가 된 경우
:
```
struct netdev *netdev_register(struct netdev_config *config)
```
위의 netdev 구성 [가이드](#2-network-manager-configuration)에 따라,
삼성 LSI Wi-Fi 드라이버 사용 방법에 대한 간단한 예를 제공합니다.
*os/drivers/wireless/<DRV_NAME>/<DRV_PREFIX>_drv_netmgr.c* 위치에 아래 코드를 구현하는 것을 권장합니다.
코드 *[slsi_drv_netmgr.c](../os/drivers/wireless/scsc/slsi_drv_netmgr.c)*를 참조하세요.
```
	struct netdev* slsidrv_register_dev(int sizeof_priv)
	{
		struct nic_io_ops nops = {slsi_linkoutput, slsi_set_multicast_list};
		struct netdev_config nconfig;
		nconfig.ops = &nops;
		nconfig.flag = NM_FLAG_ETHARP | NM_FLAG_ETHERNET | NM_FLAG_BROADCAST | NM_FLAG_IGMP;
		nconfig.mtu = CONFIG_NET_ETH_MTU; // is it right that vendor decides MTU size??
		nconfig.hwaddr_len = IFHWADDRLEN;
		nconfig.is_default = 1;
		nconfig.type = NM_WIFI;
		nconfig.t_ops.wl = &g_trwifi_drv_ops;

		void *priv = kmm_zalloc(sizeof_priv);
		if (priv == NULL) {
			return NULL;
		}
		nconfig.priv = priv;

		return netdev_register(&nconfig);
	}
```
이 등록 함수는 Wi-Fi 드라이버의 네트워크 인터페이스가 초기화될 때 호출되며,
공급업체별 netdev 구조는 Network Manager의 netdev 구조 내부에 정의된 _void *priv_에 의해 관리 및 공유될 수 있습니다.
즉, Wi-Fi 드라이버는 자신이 가지고 있는 포인터를 통해 Network Manager의 netdev에 액세스할 수도 있습니다.
하지만 Wi-Fi 드라이버에서 Network Manager의 netdev에 직접 액세스하는 것은 권장하지 않습니다.

### Wi-Fi 관리자
TizenRT는 사용자 도메인 상위 계층에서 사용되는 Wi-Fi 관리 프레임워크인 __Wi-Fi Manager__를 제공합니다.
이를 통해 애플리케이션은 Wi-Fi 드라이버를 초기화 및 초기화 취소할 수 있습니다.
무선 액세스 포인트(AP)에 연결 및 연결 해제,
이웃 AP를 스캔하고,
IP 주소 및 AP 구성과 같은 프레임워크 관련 정보를 가져오고 설정합니다.
*[wifi_utils.h](../os/include/tinyara/wifi/wifi_utils.h)*의 두 헤더 파일 및
*[wifi_manager.h](../framework/include/wifi_manager/wifi_manager.h)*는 Wi-Fi Manager의 내부적으로 사용되는 API와 공개 API 목록을 각각 설명합니다.
공급업체는 기존 또는 새로운 WiFi Manager API를 직접 수정하거나 추가할 필요가 없습니다.
그러나 이는 공급업체가 하위 계층 Wi-Fi 드라이버에서 구현해야 하는 사항이라고 할 수 있습니다.

<a id="light-weight-netlink-80211-driver"></a>
### 경량 netlink 802.11 드라이버
다음으로 TizenRT는 경량 netlink 802.11 드라이버(*lwnl80211*)를 제공합니다.
커널과 사용자 공간 프로세스 간에 정보를 전송하는 인터페이스입니다.
/disconnecting를 AP에 연결, 스캐닝, 드라이버 초기화 등 Wi-Fi 기본 기능을 제어합니다./deinit.

TizenRT에서 Wi-Fi 드라이버와 Wi-Fi 유틸리티 간의 명령 집합을 정의합니다.
그런 다음 Wi-Fi 관리자 API는 시스템 호출을 호출하여 드라이버에 명령을 보냅니다.
사용자 도메인 상위 계층에서 사용하는 표준 API를 사용합니다.
라는 이름의 Wi-Fi 유틸리티 파일을 참조하세요.
*[wifi_manager_lwnl.c](../framework/src/wifi_manager/wifi_manager_lwnl.c)*는 Wi-Fi Manager 프레임워크와 동일하게 제공됩니다.

Wi-Fi 드라이버 ops를 상위 레이어 API와 연결하기 위해,
개발자는 *os/drivers/wireless/<DRV_NAME>/<DRV_PREFIX>_drv_netmgr.c*에서 해당 드라이버 인터페이스를 생성해야 합니다.
특히, 드라이버 인터페이스는 *struct trwifi_ops*에 의해 나열된 일반적인 함수 포인터 세트인 ops를 제공해야 합니다.
여기서 각 작업은 *os/include/tinyara/net/if/wifi.h*에 정의되어 있습니다.
```
	typedef trwifi_result_e (*trwifi_init)(struct netdev *dev);
	typedef trwifi_result_e (*trwifi_deinit)(struct netdev *dev);
	typedef trwifi_result_e (*trwifi_scan_ap)(struct netdev *dev, trwifi_ap_config_s *config);
	typedef trwifi_result_e (*trwifi_connect_ap)(struct netdev *dev, trwifi_ap_config_s *config, void *arg);
	typedef trwifi_result_e (*trwifi_disconnect_ap)(struct netdev *dev, void *arg);
	typedef trwifi_result_e (*trwifi_get_info)(struct netdev *dev, trwifi_info *info);
	typedef trwifi_result_e (*trwifi_start_sta)(struct netdev *dev);
	typedef trwifi_result_e (*trwifi_start_softap)(struct netdev *dev, trwifi_softap_config_s *config);
	typedef trwifi_result_e (*trwifi_stop_softap)(struct netdev *dev);
	typedef trwifi_result_e (*trwifi_set_autoconnect)(struct netdev *dev, uint8_t chk);
	typedef trwifi_result_e (*trwifi_drv_ioctl)(struct netdev *dev, int cmd, unsigned long arg);

	struct trwifi_ops {
		trwifi_init init;
		trwifi_deinit deinit;
		trwifi_scan_ap scan_ap;
		trwifi_connect_ap connect_ap;
		trwifi_disconnect_ap disconnect_ap;
		trwifi_get_info get_info;
		trwifi_start_sta start_sta;
		trwifi_start_softap start_softap;
		trwifi_stop_softap stop_softap;
		trwifi_set_autoconnect set_autoconnect;
		trwifi_drv_ioctl drv_ioctl;
	};
```

해당 ops 기능은 기본적으로 작업을 차단한다는 점에 유의하세요.
그러나 *scan_ap*, *connect_ap* 및 *disconnect_ap*는 비차단 기능입니다.
획득한 결과에 따라 Wi-Fi 드라이버 하단에서 비차단 콜백이 필요하기 때문입니다.
예를 들어, 스캔 결과는 하위 레이어 드라이버에서 업데이트되고 전달되어야 합니다.
*lwnl80211*은 Wi-Fi 드라이버와 애플리케이션 간에 콜백 데이터를 보내고 받기 위한 대기열 시스템을 제공합니다.
여기서 대기열과 해당 API는 *[lwnl_evt_queue.c](../os/drivers/lwnl/lwnl_evt_queue.c)*에 정의되어 있습니다.
결과적으로 공급업체는 위의 대기열을 사용하여 콜백 로직을 구현해야 합니다.
내부 *os/drivers/wireless/<DRV_NAME>/<DRV_PREFIX>_drv_netmgr.c*.
개발자는 삼성 LSI 드라이버 코드 예제를 참조하는 것이 좋습니다.
*[slsi_drv_netmgr.c](../os/drivers/wireless/scsc/slsi_drv_netmgr.c)*, 참조 구현.
*static int slsi_drv_callback_handler()* 및 *static int8_t slsi_drv_scan_callback_handler()* 함수를 선언합니다.
Samsung LSI Wi-Fi 드라이버에 자체 정의된 콜백 핸들러(*linkup_handler* 및 *linkdown_handler*)에 의해 호출됩니다.

*DEBUG_LWNL80211_<driver_prefix>_ERROR* 및 *DEBUG_LWNL80211_<driver_prefix>_INFO*와 같은 로깅 시스템을 추가하려면,
[디버깅 방법](HowToDebug.md)를 참조하세요.

<a id="how-to-add-external-wi-fi-library"></a>
## 외부 Wi-Fi 라이브러리 추가 방법
공급업체는 컴파일된 Wi-Fi 라이브러리를 사용할지 여부를 선택할 수 있습니다.
TizenRT의 *wpa_supplicant*를 선택하는 대신.
외부 및 독립적으로 컴파일된 Wi-Fi 지원자 라이브러리를 지원하려면,
공급업체는 TizenRT의 출력 바이너리에 정적 라이브러리를 추가해야 합니다.
라이브러리 추가 방법에 대한 자세한 내용은 [정적 라이브러리를 추가하는 방법](HowToAddStaticLibrary.md)를 참고하세요.

<a id="incorporate-wi-fi-chipset-driver"></a>
## Wi-Fi 칩셋 드라이버 통합
Wi-Fi 드라이버 소스 파일은 새로 생성된 *os/drivers/wireless/<DRV_NAME>* 디렉터리에 있어야 합니다.
다음 하위 섹션에서는 TizenRT의 코드 트리에서 새로운 Wi-Fi 드라이버를 빌드하는 방법을 설명합니다.
이를 초기화하고 상위 TizenRT의 네트워크 관리자와 인터페이스합니다.

### Wi-Fi 드라이버 구성
위에서 설명한 대로 드라이버 소스 파일은 *os/drivers/wireless/<DRV_NAME>*에 있어야 합니다.
컴파일하고 빌드할 파일은 새로 생성된 Make.def의 *CSRCS*에 포함되어야 합니다.
드라이버 코드가 TizenRT에 설정된 구성을 사용해야 하는 경우 모든 소스 파일에tinyara/config.h.가 포함되어야 합니다.

사용자가 대상 Wi-Fi 드라이버를 선택하려면,
공급업체는 아래와 같이 Kconfig 파일에 새로운 드라이버 관련 구성 세트를 정의해야 합니다.  

1. *framework/src/wifi_manager/Kconfig*의 *CONFIG_SELECT_<DRV_NAME>_WLAN*  
2. *CONFIG_<DRV_NAME>_WLAN*(*os/drivers/wireless/<DRV_NAME>/Kconfig*)  

마지막으로 아래와 같이 새 Kconfig 파일을 *os/drivers/wireless/Kconfig*에 추가해야 합니다.
```
if DRIVERS_WIRELESS && SELECT_DRIVER_<DRV_NAME>

source drivers/wireless/<DRV_NAME>/Kconfig

endif # DRIVERS_WIRELESS
```
Make.defs는 *CONFIG_<DRV_NAME>_WLAN*과 종속성을 가져야 합니다.

### Wi-Fi 드라이버 초기화
드라이버는 메인 보드의 초기화 루틴 중에 초기화되어야 합니다.
예는 아래와 같이 *os/arch/arm/src/sidk_s5jt200/src/s5jt200_boot.c*에서 찾을 수 있습니다.
```
void board_initialize(void) {
...
#ifdef CONFIG_SCSC_WLAN
	slldbg("Initialize SCSC driver\n");
	slsi_driver_initialize();
#endif
}
```
이 절차는 드라이버의 내부 초기화에만 적용됩니다.
Network Manager에서 사용하는 네트워크 스택 및 장치를 가져오기 전에.

<a id="how-to-use-wpa_supplicant"></a>
## wpa_supplicant 사용법
공급업체는 독점(공급업체별) 신청자 코드베이스를 사용하거나
TizenRT의 Wi-Fi 솔루션을 직접 사용해보세요.

### 공급업체별 Wi-Fi 신청자 구성
Wi-Fi 관리자를 활성화하면
공급업체는 해당 Wi-Fi 라이브러리 및 Wi-Fi 드라이버를 지정해야 합니다.
위의 하위 섹션에 표시된 것처럼.
여기서는 기본적으로 공급업체별 요청자를 사용하도록 설정되어 있습니다.
공급업체는 자신의 신청자를 쉽게 활성화할 수 있습니다.
Wi-Fi Manager의 Kconfig를 추가로 수정하지 않고.

```
choice
	prompt "Wi-Fi library"
	default SELECT_PROPRIETARY_SUPPLICANT

...
config SELECT_PROPRIETARY_SUPPLICANT
	depends on !SELECT_SCSC_WLAN && !SELECT_NO_DRIVER
	bool "Enable vendor-specific supplicant"
	---help---
		Select the third party supplicant
```

### WPA 신청자
*wpa_supplicant*는 IEEE 802.11i 지원자의 오픈 소스 소프트웨어 구현입니다.
TizenRT는 *external/wpa_supplicant* 디렉터리에서 *wpa_supplicant*를 공식적으로 지원합니다.
이제 Samsung LSI 드라이버는 이를 기본 신청자 기반으로 채택합니다.

TizenRT의 *wpa_supplicant*를 사용하려면 아래 지침을 따르십시오.

1.  *외부/wpa_supplicant/Kconfig*에서 *CONFIG_DRIVER_<DRV_NAME>*를 정의합니다.
*CONFIG_DRIVER_T20*에 대해서는 아래 예를 참조하세요.
*CONFIG_SCSC_WLAN*가 이미 활성화되어 있다는 조건이 적용됩니다.
```
	config DRIVER_T20
		bool "Driver T20 for SLSI Wi-Fi"
		default y
		depends on SCSC_WLAN
```

2. *external/wpa_supplicant/src/drivers/* 아래에 *wpa_driver_ops*의 새 칩셋 드라이버 코드를 추가합니다.
이름을 *driver_<DRV_NAME>.c*로 지정합니다.
*external/wpa_supplicant/src/drivers/Make.defs*에서 빌드하려면 이 파일을 선택하세요.
새 Wi-Fi 드라이버에 대해 정의된 Kconfig에 따라 다릅니다.  
시스템 LSI Wi-Fi에 대한 예가 표시됩니다.
```
	ifeq ($(CONFIG_DRIVER_T20), y)
	CSRCS += driver_t20.c
	endif
```
*driver_<DRV_NAME>.c* 내에서 *wpa_driver_<DRV_NAME>_ops*라는 드라이버 구조 변수를 선언합니다.
*wpa_driver_<DRV_NAME>_ops*는 *wpa_driver_ops* 유형의 구조입니다.
*external/wpa_supplicant/src/drivers/driver.h*에 정의되어 있습니다.
*wpa_driver_ops* 구조에는 신청자가 연결하는 특정 Wi-Fi 드라이버에 대한 함수 포인터가 포함되어 있습니다.
예를 들어 *wpa_driver_t20_ops* 구조를 참조하세요.
Samsung LSI Wi-Fi 칩셋의 경우 *external/wpa_supplicant/src/drivers/driver_t20.c*에 정의되어 있습니다.  
```
	const struct wpa_driver_ops wpa_driver_t20_ops = {
		.name = "slsi_t20",
		.desc = "SLSI T20 Driver",
		.init2 = slsi_t20_init,
		.deinit = slsi_t20_deinit,
		.get_mac_addr = slsi_get_mac_addr,
		.get_capa = slsi_t20_get_capa,
		.scan2 = slsi_hw_scan,…
	}
```

3. 2단계에서 생성된 드라이버 구조 변수를 *wpa_drivers* 목록에 추가합니다.
*external/wpa_supplicant/src/drivers/drivers.c*에서 아래와 같이 표시됩니다.
```
	#ifdef CONFIG_DRIVER_<DRIVER_NAME>
	extern struct wpa_driver_ops wpa_driver_<DRV_NAME>_ops; /* driver_<DRV_NAME>.c */
	#endif

	const struct wpa_driver_ops *const wpa_drivers[] = {
	#ifdef CONFIG_DRIVER_<DRV_NAME>
		&wpa_driver_<DRV_NAME>_ops,
	#endif
	    ...
	}
```
그러면 신청자를 관련 Wi-Fi 드라이버에 연결합니다.  
