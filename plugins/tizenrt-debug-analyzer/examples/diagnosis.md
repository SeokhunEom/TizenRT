# 실제 QEMU 로그 분석 예제

아래 설명은 패키지의 원본 로그와 수집 당시 ELF를 확인해 작성한 예제다.
줄 번호는 raw 파일의 LF 기준이다. 자동 파서의 finding과 GPT가 수행하는
원인 해석을 구별할 수 있도록 관찰과 판단을 함께 적었다.

## 0 나누기: fault handler의 assertion이 최초 원인은 아니다

입력: [divzero.log](qemu-armv8m/raw/divzero.log)

- **관찰:** 39행에 UsageFault와 PC `0x10053078`, 40행에 `DIVBYZERO`,
  41행에 `CFAULTS=0x02000000`이 있다. 81행의 R0/R1은 각각 1/0이다.
- **해석:** 이 실행의 직접적인 fault 기전은 0으로 나누기다. 47행의
  `up_usagefault.c:137` assertion은 fault 처리 경로의 panic이다.
  assertion 파일을 최초 원인 위치로 지목하면 실제 명령 위치를 놓친다.
- **소스 확인:** 같은 ELF에서 `0x10053078`은 `crashprobe_main`, 수집 당시
  `qemu_armv8m_crashprobe.c:63`으로 해석된다. 해당 줄은 의도적으로 R1=0을
  설정한 뒤 수행하는 UDIV이다. 재현 마커도 36행에 있다.
- **복귀:** 130행에서 reboot를 요청하고 131행부터 새 부팅 진단이 나온다.
  해당 case metadata는 이후 TASH 복귀까지 관찰했다고 기록한다.
- **일반 장애의 다음 확인:** 같은 패턴을 실제 앱에서 만나면 정확한 ELF로
  fault PC를 먼저 찾고 분모 계산/입력 범위/동시 갱신을 확인한다. 이 재현의
  의도적 0 나누기 사실을 다른 사용자의 로그에 그대로 적용하지 않는다.

ELF SHA-256:
`934b21f89cf5cd3bb266d33eb23d4468a2867d01f3e24e7d827fbff420956c3b`.
수집 후 주석 수정으로 현재 checkout의 줄 번호는 다를 수 있다. 정확한 원문은
저장소 `tools/crash-corpus/evidence/20261016-qemu-armv8m/source/`의 snapshot이다.

## 스택 한계: 뒤따른 heap 손상까지 함께 보존한다

입력: [stack.log](qemu-armv8m/raw/stack.log)

39행의 UsageFault 뒤 41행 `CFAULTS=0x00100000`은 STKOF를 나타낸다.
240행에는 heap integrity 검사에서 corruption도 검출된다. 먼저 관찰된
스택 fault와 뒤따른 heap 검사 결과를 같은 사건에 남긴다. 이 로그만으로
손상 writer나 stack/handler/heap 사이의 인과관계를 하나로 확정하지 않는다.
다음 확인은 PSP/MSP·stack limit·TCB stack 범위·손상 node와 정확한 ELF의
재귀 함수 위치를 대조하는 것이다.

## MPU 접근 오류: 유효 비트가 있는 주소만 사용한다

입력: [memfault.log](qemu-armv8m/raw/memfault.log)

39행에서 PC `0x10053144`의 Memory Management Fault가 관찰된다.
42행 `CFAULTS=0x82`는 DACCVIOL과 MMARVALID를 포함하므로 이 사건의
`MMFAR=0x8002adc0`은 유효한 fault 주소다. 수집 당시 소스는 read-only로
설정한 MPU 영역에 쓰는 테스트였다. 실제 앱 분석에서는 해당 주소의 region,
권한, 접근 명령, privileged/unprivileged 실행 상태를 함께 확인해야 한다.

반대로 [execfault.log](qemu-armv8m/raw/execfault.log)의 MMFAR는 valid bit가
없으므로 주소 0을 원인 주소로 쓰지 않는다.
