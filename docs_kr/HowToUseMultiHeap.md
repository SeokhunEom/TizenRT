# 멀티힙 사용(추가)방법

멀티힙을 사용하는 경우는 두 가지가 있습니다.
- 연속적인 물리적 RAM이지만 영역을 분할하고 싶습니다.
- 분리된 물리적 RAM이지만 힙에 사용하고 싶습니다.  

## 목차
- [다중 힙 활성화](#enable-multi-heap)
- [다중 힙 사용](#use-multi-heap)


<a id="enable-multi-heap"></a>
## 다중 힙 활성화
다중 힙을 활성화하려면 아래와 같이 세 단계를 찾으십시오.

## 1. 지역 수 설정

menuconfig에는 지역 개수를 나타내는 **CONFIG_KMM_REGIONS** 값을 설정합니다.  
	```
	Memory Management -> Number of kernel memory regions -> change a number over 2
	```

## 2. 힙 수 설정

menuconfig에는 힙 개수를 나타내는 **CONFIG_KMM_NHEAPS** 값을 설정합니다.  
	```
	Memory Management -> Number of kernel heaps -> change a number over 2
	```

**REGION** 및 **HEAP**에 대한 자세한 설명은 [REGION 및 HEAP에 대한 용어](#example-of-multi-heap-usage)를 참조하세요.  

## 3. 지역 시작 주소, 크기 및 힙 인덱스 설정

menuconfig에서 시작 주소를 설정하고, 
**CONFIG_RAM_KREGIONx_START**(16진수 값, 설정된 크기 포함), **CONFIG_RAM_KREGIONx_SIZE**(새 힙의 10진수 값(바이트)) 및
는 인덱스, **CONFIG_RAM_KREGIONx_HEAP_INDEX**를 새 힙의 10진수 값으로 설정합니다. **CONFIG_RAM_KREGIONx_HEAP_INDEX**는 0부터 시작할 수 있습니다.  
```
Hardware Configuration -> Chip Selection -> An address or list of start address for kernel RAM region -> set values
Hardware Configuration -> Chip Selection -> A size or list of size for kernel RAM region -> set values
Hardware Configuration -> Chip Selection -> List of kernel heap index for RAM region -> set values
```  
각 지역은 `','`로 구분되며 모든 구성은 아래 예와 같이 `" "`에 있어야 합니다.
```
"0x02000000,0x04000000,0x07000000"
"100,400,200"
"0,1,0"
```

위의 구성을 기반으로 *up_add_kregion()* 함수는 자동으로 새 지역을 설정합니다.

<a id="use-multi-heap"></a>
## 다중 힙 사용
다중 힙을 사용하려면 아래 표시된 링크를 찾으십시오.  
[지원되는 API](#supported-apis)

<a id="example-of-multi-heap-usage"></a>
## 멀티 힙 사용의 예
3개의 물리적 RAM이 있고 아래와 같이 4개의 힙으로 사용하려는 경우입니다.  
![다중힙1](../docs/media/multiheap_1.png)  
이 경우 **CONFIG_KMM_REGIONS**는 6으로 설정되어야 하고, **CONFIG_KMM_NHEAPS**는 4로 설정되어야 합니다.  
![멀티힙2](../docs/media/multiheap_2.png)
```
CONFIG_KMM_REGIONS=6
CONFIG_KMM_NHEAPS=4
CONFIG_RAM_KREGIONx_START="0x1000,0x1400,0x3000,0x3800,0x7000,0x7100"
CONFIG_RAM_KREGIONx_SIZE="1024,512,2048,512,256,4096"
CONFIG_RAM_KREGIONx_HEAP_INDEX="0,1,1,2,2,3"
```

<a id="supported-apis"></a>
## 지원되는 API
헤더 파일 [mm.h](../os/include/tinyara/mm/mm.h)는 아래와 같이 특정 힙에 대한 메모리 할당을 지원하는 다음 API를 제공합니다.
```
void *malloc_at(int heap_index, size_t size);
void *calloc_at(int heap_index, size_t n, size_t elem_size);
void *memalign_at(int heap_index, size_t alignment, size_t size);
void *realloc_at(int heap_index, void *oldmem, size_t size);
void *zalloc_at(int heap_index, size_t size);
```
Xalloc과 Xalloc_at의 차이점은 다음과 같습니다.  
Xalloc_at는 api 인수로 전달된 특정 힙에 대해 메모리 할당을 시도합니다. 할당할 공간이 충분하지 않으면 NULL을 반환합니다.  
Xalloc은 힙 인덱스가 0인 기본 힙에 대해 메모리 할당을 시도합니다. 할당할 공간이 충분하지 않으면 전체 힙에 대해 다음 인덱스부터 순서대로 할당을 시도합니다.
