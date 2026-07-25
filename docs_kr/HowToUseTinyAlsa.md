# TinyAlsa를 사용하여 오디오 애플리케이션을 만드는 방법

TinyAlsa는 개발자가 TizenRT에서 오디오 애플리케이션을 만드는 데 사용할 수 있는 API 세트를 공개하는 작은 라이브러리입니다. TinyAlsa는 Linux ALSA 사운드 프레임워크와 호환되며 사용자 애플리케이션이 오디오 데이터를 녹음하고 재생할 수 있도록 API의 하위 집합을 공개합니다.

TinyAlsa가 처리하는 작업은 다음과 같습니다.
- 오디오 장치 열기 및 닫기
- 오디오 파이프라인 버퍼 생성, 처리 및 삭제
- 오디오 코덱을 사용한 기본 통신 및 동기화 메커니즘
- 오류 보고
- etc..

TinyAlsa는 사용자가 오디오 데이터를 캡처하고 재생할 수 있는 `pcm_open()`, `pcm_close()`, `pcm_readi()` 및 `pcm_writei()`와 같은 API를 공개합니다. 사용자가 /write 데이터를 오디오 파이프라인 버퍼로 직접 읽을 수 있는 mmap 기반 API 세트도 사용할 수 있으므로 여러 버퍼에 걸쳐 데이터를 복사하는 오버헤드를 방지하고 대기 시간을 줄일 수 있습니다. TinyAlsa에서 제공하는 API의 전체 목록과 해당 문서는 [프레임워크/include/tinyalsa/tinyalsa.h](../framework/include/tinyalsa/tinyalsa.h)에서 확인할 수 있습니다.

## 목차
- [TinyAlsa로 오디오 장치 열기 및 닫기](#open-and-close-audio-device-with-tinyalsa)
- [TinyAlsa로 오디오 녹음하기](#record-audio-with-tinyalsa)
- [TinyAlsa로 오디오 재생](#playback-audio-with-tinyalsa)
- [pcm_mmap_xxx API 사용](#use-the-pcm_mmap_xxx-apis)

<a id="open-and-close-audio-device-with-tinyalsa"></a>
## TinyAlsa로 오디오 장치 열기 및 닫기
사용자는 `pcm_open()` 또는 `pcm_open_by_name()`를 사용하여 오디오 장치를 열고 `pcm_close()`를 사용하여 오디오 장치를 닫을 수 있습니다.
오디오 장치를 여는 동안 사용자는 사용 사례 요구 사항에 따라 어떤 장치가 열려 있는지, 장치가 레코드/playback를 지원하는지 여부에 대해 주의해야 합니다. 사용자는 /dev/audio 경로에 생성된 장치 항목을 확인하여 재생 또는 녹음이 가능한 오디오 장치 목록을 확인할 수 있습니다. 이렇게 얻은 정보는 `pcm_open()`에서 녹음 또는 재생에 적합한 오디오 장치를 여는 데 사용될 수 있습니다.

`pcm_open()` 또는 `pcm_open_by_name()`의 플래그 및 구성 매개 변수는 열려 있는 오디오 장치를 구성하고 장치의 의도된 사용에 대해 TinyAlsa에 나타내는 데 사용될 수 있습니다. 장치를 열 때 구성 매개변수가 전달되지 않으면 TinyAlsa는 기본 구성 세트를 사용하여 장치를 엽니다. 장치가 열리면 해당 구성을 수정할 수 없습니다. 따라서 장치를 열 때 필요한 구성을 선택하도록 주의해야 합니다.

아래 의사코드는 기본 구성 설정으로 녹음용 오디오 장치를 열고 닫는 것을 보여줍니다.
```

#include <tinyalsa/tinyalsa.h>
...
...
//Below statement opens audio device "/dev/audio/pcmc0d0c" with default configuration
struct pcm *p = pcm_open(0, 0, PCM_IN, NULL);
...
//Perform audio recording here
...
pcm_close(p);

```

<a id="record-audio-with-tinyalsa"></a>
## TinyAlsa로 오디오 녹음
`pcm_open()`로 적합한 오디오 녹음 장치를 열면 사용자는 루프에서 `pcm_readi()`를 반복적으로 호출하여 오디오 데이터를 녹음할 수 있습니다. 사용자는 더 이상 데이터 기록을 원하지 않을 때 언제든지 루프를 종료하고 장치를 닫을 수 있습니다. pcm_readi API를 사용하는 동안 사용자는 오디오 데이터를 읽으려면 버퍼를 로컬로 할당하고 이를 API에 전달해야 합니다. 장치에서 실제로 읽은 데이터의 양을 확인하기 위해 pcm_readi API의 반환 값을 확인하는 것도 사용자의 책임입니다. pcm_readi API의 반환 값이 오류를 나타내는 경우 오류 복구를 시도하거나 오류를 무시하고 데이터 읽기를 계속하는 것은 사용자의 몫입니다.

녹음된 데이터를 채우기 위해 오디오 장치에 오디오 버퍼를 지속적으로 공급할 수 있도록 사용자는 별도의 스레드에서 오디오 데이터에 대한 모든 처리 및 저장 작업을 수행하는 것이 좋습니다. 그렇지 않으면 언더런 오류가 발생하고 오디오 데이터가 손실될 수 있습니다. 또한 장치에서 데이터를 읽을 때 대기 시간을 줄이기 위해 사용자가 더 높은 우선 순위로 오디오 녹음 스레드를 실행하는 것이 좋습니다.

아래 코드 조각은 오디오 녹음을 위한 간단한 단계를 보여줍니다.
```

#include <tinyalsa/tinyalsa.h>
...
struct pcm *p = pcm_open(0, 0, PCM_IN, NULL);
buf = malloc(size of data buffer);

while (some condition)
	frames_read = pcm_readi(p, buf, size of data buffer);
	//Check frames_read for error or data size
	//Perform error recovery if required
	//Perform processing or storage on separate thread

...
pcm_close(p);

```

<a id="playback-audio-with-tinyalsa"></a>
## TinyAlsa로 오디오 재생 TinyAlsa를 사용한
오디오 재생은 녹음 시나리오와 매우 유사합니다. 적절한 오디오 장치를 열면 사용자는 `pcm_writei()`를 반복적으로 호출하여 오디오 데이터를 재생할 수 있습니다. 사용자는 장치의 오디오 하드웨어가 특정 오디오 형식의 재생을 지원하는지 확인해야 합니다. 지원되지 않는 경우 사용자는 소프트웨어에서 헤더 구문 분석/디코딩 작업을 수행하고 `pcm_writei()`의 pcm 샘플만 전달해야 합니다.

녹음의 경우에 언급했듯이 사용자는 재생 스레드에서 오디오 데이터의 원활하고 지속적인 가용성을 보장해야 합니다. 모든 데이터 처리는 별도의 스레드에서 수행될 수 있습니다. 또한, 사용자는 API의 반환 값을 확인하여 기록된 데이터의 양이나 재생 중에 발생할 수 있는 오류를 확인해야 합니다.

아래 코드 조각은 오디오 재생을 위한 간단한 단계를 보여줍니다.
```

#include <tinyalsa/tinyalsa.h>
...
struct pcm *p = pcm_open(0, 0, PCM_OUT, NULL);
buf = malloc(size of data buffer);

while (data is available)
	//Read data into buf from file or network, etc
	ret = pcm_writei(p, buf, data size);
	//Check ret for error or amount of data written
	//Perform error recovery if required

...
pcm_close(p);

```

<a id="use-the-pcm_mmap_xxx-apis"></a>
## pcm_mmap_xxx API 사용
TinyAlsa는 사용자가 데이터를 읽거나 쓰기 위해 오디오 파이프라인 버퍼에 직접 액세스할 수 있도록 특수 API 세트를 제공합니다. 이러한 API를 사용하는 것은 오디오 녹음에 대한 위의 접근 방식에 비해 약간 복잡합니다/playback. 그러나 이러한 API는 여러 데이터 복사 단계를 제거하여 대기 시간을 줄이는 데 도움이 됩니다. 따라서 오디오를 사용하는 동안 장치에서 지연 문제가 발생하는 경우 이러한 API를 사용하는 것이 좋습니다.

mmap 기반 작업을 위해 4개의 API 세트가 제공됩니다. 각 API의 사용법은 아래에 자세히 설명되어 있습니다.

**pcm_avail_update()**
이 API는 데이터를 쓰는 데 사용할 수 있는 버퍼 공간이 얼마나 되는지 또는 데이터로 채워져 애플리케이션에서 읽을 준비가 된 버퍼 공간이 얼마나 되는지 확인하는 데 사용됩니다. 사용자는 데이터를 읽거나 쓰려고 시도하기 전에 항상 이 API를 호출해야 합니다. 사용자는 이 API로부터 0이 아닌 반환 값을 받은 경우에만 읽기 또는 쓰기 단계로 진행할 수 있습니다.

**pcm_wait()**
이 API는 여유 공간이나 데이터가 버퍼에서 사용 가능해질 때까지 기다리는 데 사용할 수 있습니다. 사용자는 이 API를 사용하여 `pcm_avail_update()`가 0을 반환할 때마다 버퍼를 기다려야 합니다.

**pcm_mmap_begin()**
**pcm_mmap_commit()**
이 두 API는 읽기 및 쓰기 작업 중에 항상 쌍으로 사용됩니다. pcm_mmap_begin API는 데이터를 읽거나 쓰는 데 사용할 수 있는 오디오 버퍼에 대한 포인터를 제공합니다. API는 읽거나 쓸 수 있는 데이터의 양도 나타냅니다. 애플리케이션이 API에 표시된 것보다 더 많은 데이터를 읽거나 쓰려고 시도하면 데이터가 손실되며 해당 작업의 결과를 예측할 수 없습니다.
읽기 또는 쓰기 작업을 수행한 후 사용자는 `pcm_mmap_commit()`를 호출하여 추가 처리를 위해 버퍼를 TinyAlsa에 반환해야 합니다.

언제든지 사용자는 자유롭게 Record/play 루프를 종료하고 pcm 장치를 닫을 수 있습니다. 또한 위의 각 API는 반환 값을 통해 오류 상태를 나타냅니다. 사용자는 이러한 반환 값을 확인하고 적절한 조치를 취해야 합니다.

**pcm_mmap_read()**
**pcm_mmap_write()**
ALSA API 목록과의 호환성을 위해 제공되는 추가 API입니다. 이러한 API는 내부적으로 mmap API를 사용하여 데이터를 전송합니다. 그러나 이러한 API를 사용하려면 사용자가 데이터 버퍼를 할당한 다음 내부적으로 이러한 버퍼의 데이터를 mmap 버퍼에 복사해야 합니다. 따라서 다른 mmap API를 직접 사용하여 제공되는 짧은 대기 시간의 이점을 제공하지 않습니다. 즉, 지연 시간 감소를 개선하기 위해 이러한 API의 사용을 피하는 것이 좋습니다.

아래 의사코드는 오디오 녹음을 위한 mmap API 사용을 간략하게 보여줍니다.
```
#include <tinyalsa/tinyalsa.h>
...
struct pcm *p = pcm_open(0, 0, PCM_IN, NULL);
while (some condition)
	if (pcm_avail_update(p) == 0)	//Data not available. So wait.
		pcm_wait(p);
		continue;

	pcm_mmap_begin(p, buf, offset, size);
	//Now perform read directly on "buf" at "offset" position for "size" bytes
	pcm_mmap_commit(p, offset, size);

...
pcm_close(p);

```
