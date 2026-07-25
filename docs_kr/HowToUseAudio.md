
# TizenRT의 오디오 프레임워크
이 문서에서는 TizenRT의 오디오 프레임워크를 구성하는 구성 요소에 대해 설명합니다.

## 목차
- [오디오 하위 시스템](#audio-subsystem)
- [오디오 코덱 드라이버](#audio-codec-drivers)
- [티냘사](#tinyalsa)
- [장치 드라이버](#device-drivers)
- [오디오 코덱 설정](#audio-codec-setup)
- [오디오 장치 등록](#audio-device-registration)

<a id="audio-subsystem"></a>
## 오디오 하위 시스템  
오디오 하위 시스템은 시스템 API와 같은 POSIX를 통해 애플리케이션에 노출된 오디오 장치의 위쪽 절반을 아래쪽 절반에 바인딩합니다.  
[os/Audio](../os/audio)에는 오디오 장치에서 POSIX 표준 API를 지원하는 오디오 인터페이스가 있습니다.
[lib/libc/audio](../lib/libc/audio)에는 오디오 파이프라인 버퍼 구현이 있습니다.  


<a id="audio-codec-drivers"></a>
## 오디오 코덱 드라이버
[os/drivers/audio](../os/drivers/audio)는 다양한 오디오 코덱의 BSP/drivers.에 대한 자리 표시자입니다. 현재 REALTEK의 ALC5658만 지원됩니다. TizenRT는 오디오 장치용 NULL 및 char 장치 드라이버도 지원합니다.  


<a id="tinyalsa"></a>
##  틴얄사  
[프레임워크/src/tinyalsa](../framework/src/tinyalsa)에는 Linux의 ALSA와 유사한 오디오 API를 제공하는 Tiny 라이브러리가 있습니다. TinyAlsa를 사용하여 오디오 애플리케이션을 개발하는 방법을 알아보려면 [사용방법TinyAlsa](HowToUseTinyAlsa.md)를 참조하세요.


<a id="device-drivers"></a>
## 장치 드라이버  
가장 일반적으로 오디오 장치는 오디오 데이터 경로에 I2S 포트를 사용하고 오디오 제어 경로를 위한 
I2C, SPI 등.  
보드 회로도에 따라 제어 경로(i2c, spi 등)와 데이터 경로(i2s 등)가 구성됩니다. [주변기기 사용 방법](HowToUsePeripheral.md)를 참조하세요.


<a id="audio-codec-setup"></a>
## 오디오 코덱 설정

Board_initialize는 제어 경로와 데이터 경로의 인스턴스로 오디오 코덱을 설정합니다.

의사 코드
```
void board_initialize()  
{  
	/* Get an i2c instance */
	i2c = initialize i2c();

	/* Get an i2s instance */
	i2s = initialize_i2s(); 

	/* Setup audio codec */
	codec_lowerhalf = initialize_audio_codec(i2c, i2s, ..);
}
```

<a id="audio-device-registration"></a>
## 오디오 장치 등록

오디오 코덱을 설정한 후 코덱의 하위 절반은 오디오 장치 상위 절반으로 둘러싸입니다.

의사코드

```
	/*Embed codec device within audio device */
	ret = audio_register(devname, codec_lowerhalf);
```

예: [os/board/artik05x/src/artik055_alc5658.c](../os/board/artik05x/src/artik055_alc5658.c)


드라이버 등록 정책 (*devname* Naming)

- 입력: /dev/pcmC[카드 ID]D[장치 ID]  
- 출력: /dev/pcmC[카드 ID]D[장치 ID]
