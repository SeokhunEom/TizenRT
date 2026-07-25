# 주변기기 사용법
TizenRT는 주변기기 사용을 위한 5가지 부품을 제공합니다. 이 문서에서는 이러한 다섯 가지 부분을 설명합니다.

## 목차
- [GPIO](#gpio)
- [UART](#uart)
- [SPI](#spi)
- [I2C](#i2c)
- [I2S](#i2s)

<a id="gpio"></a>
## GPIO
각 보드는 다음을 수행해야 합니다.

- `struct gpio_lowerhalf_s`의 인스턴스를 구현하고 노출합니다.  
- `struct gpio_ops_s`에서 지원되는 작업을 구현합니다.

### 개발자에 GPIO 등록
```
struct gpio_lowerhalf_s {
	FAR const struct gpio_ops_s *ops;
	struct gpio_upperhalf_s *parent;
};
```

```
struct [BOARD]_lowerhalf_s {
	/* 
	* Must include common member value 
	*	struct gpio_lowerhalf_s 
	*/
	FAR const struct gpio_ops_s *ops;
	struct gpio_upperhalf_s *parent;

	/* Including private value */
	...
};
```

이러한 구조체의 구현은 자체 BSP 코드에서 아래 단계를 따르는 것이 좋습니다.

```
struct [BOARD]_lowerhalf_s {
	/* 
	* Must include common member value 
	*	struct gpio_lowerhalf_s 
	*/
	FAR const struct gpio_ops_s *ops;
	struct gpio_upperhalf_s *parent;

	/* Including private value */
	...
};

FAR struct gpio_lowerhalf_s *[BOARD]_gpio_lowerhalf(gpio_pinset_t pinset)
{
	/* 
	* gpio_pinset_t : unsigned int32 value for PIN setting.
	*
	* 1. Allocate Board specific GPIO struct
	* 2. Set private value
	* 3. Set operation struct.
	*/

	return (struct gpio_lowerhalf_s *)([BOARD]_lowerhalf_s);
}
```
```
struct gpio_lowerhalf_s *lower = [BOARD]_gpio_lowerhalf(pinset);  
gpio_register(pin_number, lower);
```

이러한 단계가 제대로 작동하면 결과가 /dev/gpio[pin_number]에 표시됩니다.

### GPIO 작업 구현

```
struct gpio_ops_s {
	CODE int  (*get)(FAR struct gpio_lowerhalf_s *lower);
	CODE void (*set)(FAR struct gpio_lowerhalf_s *lower, FAR unsigned int value);
	CODE int  (*pull)(FAR struct gpio_lowerhalf_s *lower, unsigned long arg);
	CODE int  (*setdir)(FAR struct gpio_lowerhalf_s *lower, unsigned long arg);
	CODE int  (*enable)(FAR struct gpio_lowerhalf_s *lower,	int falling, int rising, gpio_handler_t handler);
	CODE int  (*ioctl)(FAR struct gpio_lowerhalf_s *lower, FAR int cmd,  unsigned long args);
};
```

```
CODE int  (*get)(FAR struct gpio_lowerhalf_s *lower);
- return : Current GPIO value

CODE void (*set)(FAR struct gpio_lowerhalf_s *lower, FAR unsigned int value);
- value : GPIO value to be set.

CODE int  (*pull)(FAR struct gpio_lowerhalf_s *lower, unsigned long arg);
- arg : GPIO_DRIVE_PULLUP / GPIO_DRIVE_PULLDOWN / GPIO_DRIVE_FLOAT
- retrun : OK / negative value 

CODE int  (*setdir)(FAR struct gpio_lowerhalf_s *lower, unsigned long arg);
- arg : GPIO_DIRECTION_NONE / GPIO_DIRECTION_OUT / GPIO_DIRECTION_IN
- retrun : OK / negative value 

CODE int  (*enable)(FAR struct gpio_lowerhalf_s *lower, int falling, int rising, gpio_handler_t handler);
- falling : true / false
- rising : true / false
- gpio_handler_t handler : This handler should be called whenever each GPIO interrupt occurred.
	-> typedef CODE void (*gpio_handler_t)(FAR struct gpio_upperhalf_s *upper);
```

[enable] 작업에는 아래 프로세스가 포함되어야 합니다.
```
static int [Interrupt_Function](int irq, FAR void *context, FAR void *arg);
- irq : irqvector number
- context : identifying value
- arg : user specific pointer

if (handler) {
	irq_attach([irqvector], [Interrupt_Function], (void *)arg);
	up_enable_irq([irqvector]);
} else {
	up_disable_irq([irqvector]);
	irq_detach([irqvector]);
}
```

예제 구현은 os/arch/arm/src/s5j 폴더 아래의 s5j_gpio_lowerhalf.c에 있습니다.

<a id="uart"></a>
## UART
UART는 초기 콘솔 설정과 직렬 콘솔 드라이버, 직렬 포트 드라이버 설정의 두 부분으로 구성됩니다.

### 초기 콘솔 설정
#### up_earlyserialinit 기능 추가
부팅 코드는 OS 시작 전에 이를 호출하여 부팅 중에 낮은 수준의 디버그 메시지를 제공해야 합니다. 탭 아래
```
void up_earlyserialinit(void)
{
	...
	/* Configure whichever one is the console */

#ifdef HAVE_SERIAL_CONSOLE
	CONSOLE_DEV.isconsole = true;
	CONSOLE_DEV.ops->setup(&CONSOLE_DEV);
#endif
}
```
[탭 아래](#add-uart-device-structure)에서 CONSOLE_DEV를 참조하세요.

#### up_putc 및 up_lowputc 기능 추가
MCU 레지스터에 액세스하여 문자 전송을 제공합니다.
```
int up_putc(int ch)
{
#if defined(HAVE_SERIAL_CONSOLE)
	/* Check for LF */
	if (ch == '\n') {
		/* Add CR */
		up_lowputc('\r');
	}

	up_lowputc(ch);
#endif

	return ch;
}

void up_lowputc(char ch)
{
#ifdef HAVE_SERIAL_CONSOLE
	/* Wait until the TX FIFO is not full */

	while ((getreg32(XXX_CONSOLE_BASE + XXX_UART_FR_OFFSET) & UART_FR_TXFF) != 0) ;

	/* Then send the character */

	putreg32((uint32_t) ch, XXX_CONSOLE_BASE + XXX_UART_DR_OFFSET);
#endif
}
```

### 직렬 콘솔 드라이버, 직렬 포트 드라이버 설정
#### up_serialinit 기능 추가
이 함수는 문자 UART 장치 드라이버를 등록합니다.  
Os는 **up_serialinit**를 호출하여 *up_initialize*를 통해 아키텍처별 기능을 초기화합니다.
```
void up_serialinit(void)
{
	uart_register("dev/console", &CONSOLE_DEV);
	uart_register("dev/ttyS0", &TTYS0_DEV);
	uart_register("dev/ttyS1", &TTYS1_DEV);
	...
}
```

<a id="add-uart-device-structure"></a>
#### UART 장치 구조 추가
```CONSOLE_DEV``` 및 ```TTYSx_DEV```는 *uart_dev_t*라는 UART 장치 드라이버 구조 유형입니다.  
*os/include/tinyara/serial* 폴더 아래 **[serial.h](../os/include/tinyara/serial/serial.h)**에 정의되어 있습니다.
```
struct uart_dev_s {
	...
	/* I/O buffers */

	struct uart_buffer_s xmit;	/* Describes transmit buffer */
	struct uart_buffer_s recv;	/* Describes receive buffer */

	/* Driver interface */

	FAR const struct uart_ops_s *ops;	/* Arch-specific operations */
	FAR void *priv;				/* Used by the arch-specific logic */
	...
};

typedef struct uart_dev_s uart_dev_t;
```

**xmit**, **recv**, **ops** 및 **priv**를 정의해야 합니다.  
- **xmit** : 전송 버퍼의 크기 및 주소  
- **recv** : 수신 버퍼의 크기 및 주소  
- **ops** : 설정, 전송, 수신 등과 같은 작업 테이블  
- **priv** : 전송 속도, 인터럽트 번호, 패리티 등과 같은 장치의 개인 데이터

다음은 예제 코드입니다.
```
#define CONSOLE_DEV		g_uart0port		/* UART0 is console */

static uart_dev_t g_uart0port = {
	.recv = {
		.size	= CONFIG_UART0_RXBUFSIZE,
		.buffer	= g_uart0rxbuffer,
	},
	.xmit = {
		.size	= CONFIG_UART0_TXBUFSIZE,
		.buffer	= g_uart0txbuffer,
	},
	.ops		= &g_uart_ops,
	.priv		= &g_uart0priv,
};

static struct up_dev_s g_uart0priv = {
	.uartbase	= UART0_BASE,
	.baud		= CONFIG_UART0_BAUD,
	.irq		= IRQ_UART0,
	.parity		= CONFIG_UART0_PARITY,
	.bits		= CONFIG_UART0_BITS,
	.stopbits2	= CONFIG_UART0_2STOP,
	.rxd		= GPIO_UART0_RXD,
	.txd		= GPIO_UART0_TXD,
	.pclk		= CLK_GATE_UART0_PCLK,
	.extclk		= CLK_GATE_UART0_EXTCLK,
};
```

<a id="spi"></a>
## SPI
하위 수준 SPI 드라이버에 의해 구현될 모든 구조 및 기능 프로토타입은 *os/include/tinyara/spi* 폴더 아래 헤더 파일 **[spi.h](../os/include/tinyara/spi/spi.h)**에 제공됩니다.

### SPI 장치 드라이버 초기화
각 SPI 장치 드라이버는 SPI 장치 초기화를 위해 아래 프로토타입의 정의를 구현해야 합니다.
```
FAR struct spi_dev_s *up_spiinitialize(int port);
```

구현 예는 **[s5j_spi.c](../os/arch/arm/src/s5j/s5j_spi.c)**에 있습니다.
*os/arch/arm/src/s5j* 폴더 아래에 있습니다.
```
struct spi_dev_s *up_spiinitialize(int port)
{
	FAR struct s5j_spidev_s *priv = NULL;

	if (port < 0 || port >= SPI_PORT_MAX) {
		return NULL;
	}

	switch (port) {
	case 0:
		priv = &g_spi0dev;
		break;

	...

	}

	...

	return (struct spi_dev_s *)priv;
}
```

낮은 수준(하드웨어별) SPI 장치 드라이버는 *struct spi_dev_s*의 인스턴스를 생성하고 위에 표시된 대로 상위 수준 장치 드라이버로 반환되어야 합니다. 또한 SPI 드라이버는 *struct spi_ops_s*의 인스턴스를 생성하고 이를 *struct spi_dev_s* 인스턴스의 *ops* 멤버에 연결해야 합니다.
```
struct spi_dev_s {
	FAR const struct spi_ops_s *ops;
};

struct spi_ops_s {
#ifndef CONFIG_SPI_OWNBUS
	int (*lock)(FAR struct spi_dev_s *dev, bool lock);
#endif
	void (*select)(FAR struct spi_dev_s *dev, enum spi_dev_e devid, bool selected);
	uint32_t (*setfrequency)(FAR struct spi_dev_s *dev, uint32_t frequency);
	void (*setmode)(FAR struct spi_dev_s *dev, enum spi_mode_e mode);
	void (*setbits)(FAR struct spi_dev_s *dev, int nbits);
	uint8_t (*status)(FAR struct spi_dev_s *dev, enum spi_dev_e devid);
#ifdef CONFIG_SPI_CMDDATA
	int (*cmddata)(FAR struct spi_dev_s *dev, enum spi_dev_e devid, bool cmd);
#endif
	uint16_t (*send)(FAR struct spi_dev_s *dev, uint16_t wd);
#ifdef CONFIG_SPI_EXCHANGE
	void (*exchange)(FAR struct spi_dev_s *dev, FAR const void *txbuffer, FAR void *rxbuffer, size_t nwords);
#else
	void (*sndblock)(FAR struct spi_dev_s *dev, FAR const void *buffer, size_t nwords);
	void (*recvblock)(FAR struct spi_dev_s *dev, FAR void *buffer, size_t nwords);
#endif
	int (*registercallback)(FAR struct spi_dev_s *dev, spi_mediachange_t callback, void *arg);
};

```

위의 구조 *struct spi_ops_s*는 하위 수준 SPI 장치 드라이버에 의해 구현될 함수 포인터 목록을 정의합니다.
구현 예는 *os/arch/arm/src/s5j* 폴더 아래의 **[s5j_spi.c](../os/arch/arm/src/s5j/s5j_spi.c)**에 있습니다.
```
static const struct spi_ops_s g_spiops = {
#ifndef CONFIG_SPI_OWNBUS
	.lock = spi_lock,
#endif
	.select = spi_select,
	.setfrequency = spi_setfrequency,
	.setmode = (void *)spi_setmode,
	.setbits = (void *)spi_setbits,
	.status = 0,
#ifdef CONFIG_SPI_CMDDATA
	.cmddata = 0,
#endif
	.send = spi_send,
#ifdef CONFIG_SPI_EXCHANGE
	.exchange = spi_exchange,
#else
	.sndblock = spi_sndblock,
	.recvblock = spi_recvblock,
#endif
	.registercallback = 0,
};

static int spi_lock(struct spi_dev_s *dev, bool lock)
{
	FAR struct s5j_spidev_s *priv = (FAR struct s5j_spidev_s *)dev;

        ...

	return OK;
}

static void spi_select(struct spi_dev_s *dev, enum spi_dev_e devid, bool selected)
{
	FAR struct s5j_spidev_s *priv = (FAR struct s5j_spidev_s *)dev;

	...

}
```

<a id="i2c"></a>
## I2C

각 이사회는 다음을 수행해야 합니다.

- 구조체 i2c_dev_s의 인스턴스를 구현하고 노출합니다.     
- i2c 구조체 i2c_op_s에서 지원되는 작업을 구현합니다.  


```
struct i2c_dev_s {
	const struct i2c_ops_s *ops;	/* I2C vtable */
	FAR void *priv;			/* Used by the arch-specific logic */
}; 
``` 
  
```
struct i2c_ops_s {
	uint32_t (*setfrequency)(FAR struct i2c_dev_s *dev, uint32_t frequency);
	int (*setaddress)(FAR struct i2c_dev_s *dev, int addr, int nbits);
	int (*write)(FAR struct i2c_dev_s *dev, const uint8_t *buffer, int buflen);
	int (*read)(FAR struct i2c_dev_s *dev, uint8_t *buffer, int buflen);
#ifdef CONFIG_I2C_WRITEREAD
	int (*writeread)(FAR struct i2c_dev_s *inst, const uint8_t *wbuffer, int wbuflen, uint8_t *rbuffer, int rbuflen);
#endif

#ifdef CONFIG_I2C_TRANSFER
	int (*transfer)(FAR struct i2c_dev_s *dev, FAR struct i2c_msg_s *msgs, int count);
#endif
#ifdef CONFIG_I2C_SLAVE
	int (*setownaddress)(FAR struct i2c_dev_s *dev, int addr, int nbits);

	int (*registercallback)(FAR struct i2c_dev_s *dev, int (*callback)(void));
#endif
};
```

위 선언은 **[i2c.h](../os/include/tinyara/i2c.h)**에 있습니다.  
  
위 외에도 각 보드 수준 로직은 **[i2c.h](../os/include/tinyara/i2c.h)**에 나열된 다른 기능을 구현해야 합니다.  

```
EXTERN FAR struct i2c_dev_s *up_i2cinitialize(int port);
EXTERN int up_i2cuninitialize(FAR struct i2c_dev_s *dev);
EXTERN int up_i2creset(FAR struct i2c_dev_s *dev);
```  

### I2C 사용자 수준 I/O
i2c 포트에 사용자 수준 I/O가 필요한 경우 보드 수준 로직은 위의 i2c_ops에서 언급한 등록 및 fs 작업(읽기, 쓰기, 쓰기 읽기)에 대해 다음을 제공해야 합니다.  

```
#ifdef CONFIG_I2C_USERIO
int i2c_uioregister(FAR const char *path, FAR struct i2c_dev_s *dev);
#endif
```
> **Note**
> i2c 사용자 I/O는 아직 시기상조라는 점에 유의하세요.

### I2C 초기화  
I2C는 up_i2cinitialize를 호출하여 초기화됩니다.  
초기화는 하나의 i2c 포트를 사용하고 i2c 장치 구조를 반환합니다.  
초기화는 _up_initialize()_의 일부 또는 _board_initialize()_의 일부로 수행됩니다.  

i2c 함수 및 선언에 대한 자세한 내용은 **[i2c.h](../os/include/tinyara/i2c.h)**를 참조하세요.  

<a id="i2s"></a>
## I2S

보드용 I2S 포트에는 다음이 포함됩니다.
- I2S 장치 구조 및 작동.
- I2S 초기화 및 장치 액세스.  

### I2S 장치 구조 및 작동
각 이사회는 다음을 수행해야 합니다.
- 각 포트에 대해 구조체 i2s_dev_s의 인스턴스를 구현하고 노출합니다.     
- i2s에서 지원되는 작업을 구현합니다(예: 구조체 i2s_op_s). 
관련 데이터 구조는 다음과 같습니다. 

```
/* I2S private data.  This structure only defines the initial fields of the
 * structure visible to the I2S client.  The specific implementation may
 * add additional, device specific fields
 */

struct i2s_dev_s {
	FAR const struct i2s_ops_s *ops;
};

struct i2s_ops_s {
	/* Receiver methods */

	CODE uint32_t (*i2s_rxsamplerate)(FAR struct i2s_dev_s *dev, uint32_t rate);
	CODE uint32_t (*i2s_rxdatawidth)(FAR struct i2s_dev_s *dev, int bits);
	CODE int (*i2s_receive)(FAR struct i2s_dev_s *dev, FAR struct ap_buffer_s *apb, i2s_callback_t callback, FAR void *arg, uint32_t timeout);

	/* Transmitter methods */

	CODE uint32_t (*i2s_txsamplerate)(FAR struct i2s_dev_s *dev, uint32_t rate);
	CODE uint32_t (*i2s_txdatawidth)(FAR struct i2s_dev_s *dev, int bits);
	CODE int (*i2s_send)(FAR struct i2s_dev_s *dev, FAR struct ap_buffer_s *apb, i2s_callback_t callback, FAR void *arg, uint32_t timeout);

	/* Errors handling methods */

	CODE int (*i2s_err_cb_register)(FAR struct i2s_dev_s *dev, i2s_err_cb_t cb, FAR void *arg);

	/* Generic stop method */
	CODE int (*i2s_stop)(FAR struct i2s_dev_s *dev);
};
```

위 선언은 **[i2s.h](../os/include/tinyara/audio/i2s.h)**에 있습니다.  
각 보드는 위의 각 작업을 구현해야 합니다.

s5j 칩셋에서 i2s 작업의 샘플 구현은 **[s5j_i2s.c](../os/arch/arm/src/s5j/s5j_i2s.c)**를 참조하세요.
아래 s5j_i2s.c의 샘플 코드, 모든 작업은 정적 함수로 구현됨
는 i2s 장치 작업에 연결됩니다.
```
	/* static function to set sample rate */
static uint32_t i2s_samplerate(struct i2s_dev_s *dev, uint32_t rate)
{
	...
}
	/* Static function to set each sample data width */
static uint32_t i2s_txdatawidth(struct i2s_dev_s *dev, int bits)
{
	...
}
	/* Wrapping static implementation of i2s operations with i2s device */
static const struct i2s_ops_s g_i2sops = {
	/* Receiver methods */

	.i2s_rxsamplerate = i2s_samplerate,
	.i2s_rxdatawidth = i2s_rxdatawidth,
	.i2s_receive = i2s_receive,

	/* Transmitter methods */

	.i2s_txsamplerate = i2s_samplerate,
	.i2s_txdatawidth = i2s_txdatawidth,
	.i2s_send = i2s_send,

	.i2s_stop = i2s_stop,
	.i2s_err_cb_register = i2s_err_cb_register,
};
```
### I2S 초기화 및 장치 액세스
보드는 ```struct i2s_dev_s* xxx_i2s_initialize(uint16_t port)```를 내보내 i2s 포트를 초기화하고 해당 장치 구조를 가져옵니다.    
>**NOTE**  
> s5j_i2s.c의 현재 코드에는 ```struct i2s_dev_s* s5j_i2s_initialize(uint16_t port)```로 초기화 기능이 있습니다. 

이 i2s 초기화 함수는 해당 장치 구조를 가져오기 위해 포트 번호와 함께 호출되어야 합니다.

