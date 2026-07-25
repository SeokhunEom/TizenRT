# API 이식할 목록

## 목차
- [API 목록](#api-list)
- [API 설명](#api-description)

<a id="api-list"></a>
## API 목록
1. up_flashinitialize()
2. up_idle()
3. up_enable_irq()
4. up_netinitialize()
5. up_serialinit()
6. up_earlyserialinit()
7. up_putc()
8. up_getc()
9. up_puts()
10. up_lowputc()
11. up_timer_initialize()
12. up_wdog_init()
13. up_wdog_keepalive()
14. up_watchdog_disable()
15. up_i2cinitialize()
16. up_spiinitialize()
17. up_rtc_initialize()
18. up_reboot_reason_init()
19. up_reboot_reason_read()
20. up_reboot_reason_write()
21. up_reboot_reason_clear()
22. up_check_prodswd() 및 up_check_proddownload()

<a id="api-description"></a>
## API 설명

- 초기화 MTD 장치 인스턴스를 생성하려면 하위 수준 칩 전용 API **up_flashinitialize()**를 추가하세요. MTD 장치는 파일 시스템에 등록되지 않지만 다른 기능(예: 블록 또는 문자 드라이버 프런트 엔드)에 바인딩될 수 있는 인스턴스로 생성됩니다.
```c
 * Prototype: struct mtd_dev_s *up_flashinitialize(void)
 * Input Parameters:
 *   none
 * Returned Value:
 *   Valid MTD device structure reference on success; a NULL on failure
```
*파일 위치:* "os/arch/arm/src/chip/chip_flash.c"

*프로토타입 위치:* "os/include/tinyara/fs/mtd.h"

- 다른 실행 준비 작업이 없을 때 실행될 하위 수준 칩 특정 API **up_idle()**를 추가합니다. 이는 프로세서 유휴 시간이며 일부 인터럽트가 발생하여 유휴 작업에서 컨텍스트 전환이 발생할 때까지 계속됩니다. 이 상태의 처리는 프로세서별로 다를 수 있습니다.
예를 들어, 여기에서 전원 관리 작업이 수행될 수 있습니다.
```c
 * Prototype: void up_idle(void)
 * Input Parameters:
 *   none
 * Returned Value:
 *   none
```
여기에 **up_idlepm()**를 추가하여 IDLE 상태 전원 관리를 수행할 수 있습니다.
```c
static void up_idlepm(void)
```
*파일 위치:* "os/arch/arm/src/chip/chip_idle.c"

*프로토타입 위치:* "os/include/tinyara/arch.h"

- 저수준 칩 전용 API **up_enable_irq()**를 추가하여 'irq'로 지정된 IRQ를 활성화합니다.
```c
 * Prototype: void up_enable_irq(int irq)
 * Input Parameters:
 *   none
 * Returned Value:
 *   none
```
*파일 위치:* "os/arch/arm/src/chip/chip_irq.c"

*프로토타입 위치:* "os/include/tinyara/arch.h"

- 네트워크 도메인을 초기화하려면 하위 수준 칩 전용 API **up_netinitialize()**를 추가하세요.
[TO DO]
```c
 * Prototype: void up_netinitialize(void)
 * Input Parameters:
 *   none
 * Returned Value:
 *   none
```
*파일 위치:* "os/arch/arm/src/chip/chip_enet.c"

*프로토타입 위치:* "os/arch/arm/src/common/up_internal.h"

- 로우 레벨 칩 특정 API를 추가하여 직렬 콘솔 및 포트를 초기화하고 등록합니다.

**up_serialinit()** : 직렬 콘솔 및 직렬 포트를 등록합니다.
```c
 * Prototype: void up_serialinit(void)
 * Input Parameters:
 *   none
 * Returned Value:
 *   none
```
b. **up_earlyserialinit()** : 디버그 초기에 낮은 수준의 UART 초기화를 수행하여
부팅 중에 직렬 콘솔을 사용할 수 있습니다.  이는 up_serialinit 이전에 호출되어야 합니다.
```c
 * Prototype: void up_earlyserialinit(void)
 * Input Parameters:
 *   none
 * Returned Value:
 *   none
```
*파일 위치:* "os/arch/arm/src/chip/chip_serial.c"

*프로토타입 위치:* ""

- UART를 통해 문자를 수신하고 전송하려면 하위 수준 칩 특정 API를 추가하세요.

**up_putc()** : 직렬 콘솔에 1바이트 출력
```c
 * Prototype: int up_putc(int ch)
 * Input Parameters:
 *   ch - chatacter to output
 * Returned Value:
 *   sent character
```
b. **up_lowputc()** : 직렬 콘솔에 1바이트를 출력합니다.
```c
 * Prototype: void up_lowputc(void)
 * Input Parameters:
 *   character to output
 * Returned Value:
 *   none
```
다. **up_getc()** : 직렬 콘솔에서 1바이트 읽기
```c
 * Prototype: int up_getc(void)
 * Input Parameters:
 *   none
 * Returned Value:
 *   int value, -1 if error, 0~255 if byte successfully read
```
디. **up_puts()** : 직렬 콘솔의 출력 문자열
```c
 * Prototype: void up_puts(const char *str)
 * Input Parameters:
 *   str - string to output
 * Returned Value:
 *   none
```
*파일 위치:* "os/arch/arm/src/chip/chip_serial.c"

- 타이머 인터럽트를 초기화하기 위해 시작 중에 호출되는 하위 수준 칩 특정 API **up_timer_initialize()**를 추가합니다.
```c
 * Prototype: void up_timer_initialize(void)
 * Input Parameters:
 *   none
 * Returned Value:
 *   none
```
*파일 위치:* "os/arch/arm/src/chip/chip_timerisr.c"

*프로토타입 위치:* "os/include/tinyara/arch.h"

- 워치독을 활성화, 비활성화 및 초기화하려면 하위 수준 칩 특정 API를 추가하세요.

**up_wdog_init()** : irq에 대한 워치독 초기화
CONFIG_WATCHDOG_FOR_IRQ가 활성화된 경우에만 추가됩니다.
```c
 * Prototype: void up_wdog_init(uint16_t timeout)
 * Input Parameters:
 *   timeout - timeout for watchdog to expire
 * Returned Value:
 *   none
```
b. **up_wdog_keepalive()** : 타임아웃 방지를 위해 워치독 타이머를 재설정합니다.
```c
 * Prototype: void up_wdog_keepalive(void)
 * Input Parameters:
 *   none
 * Returned Value:
 *   none
```
다. **up_watchdog_disable()** : 보드별 워치독 비활성화
```c
 * Prototype: void up_watchdog_disable(void)
 * Input Parameters:
 *   none
 * Returned Value:
 *   none
 * Cautions:
 *   This can be only used if we cannot use driver structure like assert.
```
*파일 위치:* "os/arch/arm/src/chip/chip_wdog.c"

*프로토타입 위치:* "os/include/tinyara/arch.h"

- 하위 레벨 칩 특정 API **up_i2cinitialize()**를 추가하여 하나의 I2C 버스를 초기화합니다. 
```c
 * Prototype: struct i2c_dev_s *up_i2cinitialize(int port)
 * Input Parameters:
 *   Port number (for hardware that has multiple I2C interfaces)
 * Returned Value:
 *   Valid I2C device structure reference on success; a NULL on failure
```
*파일 위치:* "os/arch/arm/src/chip/chip_i2c.c"

- 하위 레벨 칩 특정 API **up_spiinitialize()**를 추가하여 선택한 SPI 버스를 초기화합니다.
```c
 * Prototype: struct spi_dev_s *up_spiinitialize(int port)
 * Input Parameters:
 *   Port number (for hardware that has multiple SPI interfaces)
 * Returned Value:
 *   Valid SPI device structure reference on success; a NULL on failure
```
*파일 위치:* "os/arch/arm/src/chip/chip_spi.c"

- 하위 수준 칩 특정 API **up_rtc_initialize()**를 추가하여 선택한 구성에 따라 하드웨어 RTC를 초기화합니다.
```c
 * Prototype: int up_rtc_initialize(void)
 * Input Parameters:
 *   none
 * Returned Value:
 *   Zero (OK) on success; a negated errno on failure
```
*파일 위치:* "os/arch/arm/src/chip/chip_rtc.c"

*프로토타입 위치:* "os/include/tinyara/arch.h"

- 직렬 콘솔에서 1바이트를 전송하려면 하위 수준 칩 전용 API **up_lowputc()**를 추가하세요.
```c
 * Prototype: void up_lowputc(void)
 * Input Parameters:
 *   character to output
 * Returned Value:
 *   none
```
*파일 위치:* "os/arch/arm/src/chip/chip_serial.c"

*프로토타입 위치:* "os/arch/arm/src/common/up_internal.h"

- 재부팅 이유 레지스터를 초기화하려면 하위 레벨 칩 특정 API **up_reboot_reason_init**를 추가하세요.
```c
 * Prototype: void up_reboot_reason_init(void)
 * Input Parameters:
 *   none
 * Returned Value:
 *   none
```
*파일 위치:* "os/arch/arm/src/chip/chip_reboot_reason.c"

*프로토타입 위치:* "os/arch/arm/include/reboot_reason.h"

- 저레벨 칩 전용 API **up_reboot_reason_read**를 추가하여 레지스터에서 재부팅 이유를 읽습니다.
```c
 * Prototype: void up_reboot_reason_read(void)
 * Input Parameters:
 *   none
 * Returned Value:
 *   reboot reason code
```
*파일 위치:* "os/arch/arm/src/chip/chip_reboot_reason.c"

*프로토타입 위치:* "os/arch/arm/include/reboot_reason.h"

- 등록에 재부팅 이유를 작성하려면 하위 레벨 칩 특정 API **up_reboot_reason_write**를 추가하세요.
```c
 * Prototype: void up_reboot_reason_write(void)
 * Input Parameters:
 *   reboot reason code
 * Returned Value:
 *   none
```
*파일 위치:* "os/arch/arm/src/chip/chip_reboot_reason.c"

*프로토타입 위치:* "os/arch/arm/include/reboot_reason.h"

- 재부팅 이유 레지스터를 지우려면 하위 수준 칩 특정 API **up_reboot_reason_clear**를 추가하세요.
```c
 * Prototype: void up_reboot_reason_clear(void)
 * Input Parameters:
 *   none
 * Returned Value:
 *   none
```
*파일 위치:* "os/arch/arm/src/chip/chip_reboot_reason.c"

*프로토타입 위치:* "os/arch/arm/include/reboot_reason.h"

- 하위 레벨 칩 특정 API **up_check_prodswd** 및 **up_check_proddownload**를 추가하여 확인하세요. JTAG 사용 및 다운로더 사용.  
자세한 내용은 삼성 TizenRT 개발자에게 문의하세요.
```c
 * Prototype: int up_check_prodswd(void)
 * Input Parameters:
 *   none
 * Returned Value:
 *   OK for success, ERROR for failure.
```
```c
 * Prototype: int up_check_proddownload(void)
 * Input Parameters:
 *   none
 * Returned Value:
 *   OK for success, ERROR for failure.
```
*프로토타입 위치:* "os/arch/arm/include/prodconfig.h"
