# ARM 언와인드 테이블과 TizenRT PR #7522·#7445 도입 검토

작성일: 2026-09-18\
대상: ARM 호출 스택 복원 개념, 두 PR의 실제 구현, 차이점, 도입 우선순위\
검토 방법: GitHub의 PR 메타데이터·전체 diff·고정 head 소스·공개 리뷰·CI 상태와 ARM/GCC 공식 자료 확인\
검증 범위: **소스 및 규격 검토. 이 문서를 작성하면서 펌웨어를 빌드하거나 보드에서 실행하지는 않았다.**

읽을 부분 바로가기: [개념 설명](#basics) · [#7522 상세](#pr7522-detail) · [#7445 상세](#pr7445-detail) · [차이점](#comparison) · [비용](#cost) · [도입 순서](#adoption) · [검증 계획](#validation)

## 1. 먼저 읽을 결론

두 PR 모두 언와인드 테이블을 사용하지만, **해결하려는 문제가 다르다.**

- **[#7522: Arm unwinder support in TizenRT][pr7522]**: 크래시나 ASSERT가 발생했을 때 커널이 호출 경로를 출력하는 기능이다. ARM EHABI 테이블을 직접 해석하는 엔진과 loadable binary 테이블 등록을 추가한다.
- **[#7445: heapinfo capture window][pr7445]**: 특정 시간 구간의 메모리 할당·재할당·해제를 기록하고, 남아 있는 할당의 생성 경로를 보여주는 기능이다. 사용자 영역 호출 스택 수집에 libgcc의 `_Unwind_Backtrace()`를 사용한다.

**범용 크래시 분석을 위한 기반 도입이라면 #7522의 방향을 우선한다. 다만 현재 head를 그대로 제품에 도입하는 것은 권하지 않는다.** IRQ 스택 경계, 앱 재시작 시 등록 수명, 초기 레지스터 상태, 스캔 결과의 표시, 선택 가능한 빌드 구성에 수정이 필요하다. “방향을 선택한다”와 “현재 구현이 도입 가능한 품질이다”는 별개의 판단이다.

목표가 메모리 누수 후보의 할당 경로 조사라면 #7445가 더 직접적이다. 그러나 이 PR도 호출 스택 저장 순서와 캡처 종료 경계 등에 문제가 있어, 그대로 도입하면 보고서가 잘못된 경로 또는 부정확한 집계를 보여줄 수 있다.

| 원하는 결과 | 우선 검토할 변경 | 판단 |
| --- | --- | --- |
| ASSERT/Data Abort 당시 커널·앱 호출 경로 | #7522 | 기반 후보. 아래 필수 수정 및 보드 검증 후 도입 |
| 특정 작업 전후에 남은 메모리와 할당 경로 | #7445 | 진단 도구 후보. 기록·집계 결함을 먼저 수정 |
| 두 기능 모두 | 기반 API와 테이블 관리를 정리한 뒤 단계별 통합 | 두 PR을 단순히 연속 cherry-pick하지 않음 |
| BK7239N 등 Cortex-M33 보드에서 즉시 사용 | 어느 쪽도 즉시 적용 가능하다고 판단할 수 없음 | 두 PR의 주된 실제 연결 대상은 RTL8730E/AMEBASMART의 ARMv7-A 경로 |

이 권고는 **현재 코드의 설계·정확성·유지보수 비용을 비교한 판단**이며, 실제 장비의 성능·메모리 증가량을 측정한 결과는 아니다.

### 1.1 검토한 정확한 버전

PR의 본문은 의도를 설명하지만 최신 코드와 항상 일치하지 않는다. 특히 #7445는 본문에 등장하는 필드와 실행 범위가 최신 head와 다르므로, 아래 버전의 코드를 기준으로 설명한다.

| 항목 | #7522 | #7445 |
| --- | --- | --- |
| 확인 당시 상태 | Open, draft 아님, 미병합 | Open, draft 아님, 미병합 |
| head SHA | `cfdc05ec99e7a97a44dd3c93066d0d64ffe115dc` | `c6b7d2c1ff14f6744dec3ec4aab49c021b0bbb4f` |
| GitHub API가 반환한 base SHA | `34db6c44e7d6402d5442fe3baa56bc2b39de3632` | `7f257954e75e4e01323102244d2c6e551e073e21` |
| 커밋 수 | 2 | 2 |
| 변경 파일 | 39개, +2,417 / −24줄 | 27개, +1,499 / −55줄 |
| 공개 리뷰 | review/review comment 조회 결과 없음 | COMMENTED 리뷰 3개. 공백 제거·utils 커밋 분리 요청과 수정 답변 |
| head에 보고된 CI | check run 1개 success, commit status 20개 success | check run 1개 success, commit status 20개 success |

근거: [#7522 PR API][api7522], [#7445 PR API][api7445], [#7522 CI][ci7522], [#7445 CI][ci7445]. base SHA는 조회 시점의 대상 브랜치 정보다. 두 PR이 같은 기준점에서 만들어진 대체 패치라고 가정하지 않는다.

CI 성공은 해당 CI 구성의 성공 증거다. **언와인더를 활성화한 모든 설정, 크래시 시 호출 경로의 정확성, SMP 경쟁 조건, 실제 보드 실행까지 증명하는 것은 아니다.** #7522에는 작성자가 제시한 실행 로그가 있으나 이번 검토에서 독립적으로 재현하지 않았다.

### 1.2 문서의 근거 구분

- **소스 사실**: 고정 head에서 직접 확인되는 조건문, 자료구조, 호출 순서 등.
- **규격 비교**: 공식 ABI와 구현을 비교해 확인한 지원 범위 및 불일치.
- **실행 시나리오 분석**: 코드상 가능한 실행 순서로 설명한 위험. 실제 보드 재현과 구분한다.
- **추가 검증 사항**: 컴파일러·보드·빌드 설정에 따라 결과가 달라져 실행 검증이 필요한 항목.

<a id="basics"></a>

## 2. 언와인드 테이블을 처음 접하는 사람을 위한 설명

### 2.1 왜 “어디에서 죽었는가”만으로는 부족할까?

다음과 같은 프로그램을 생각해 보자. 아래 함수와 주소는 설명을 위한 가상 예시다.

```text
main()
  → download_file()
      → parse_response()
          → copy_field()  ← 여기서 잘못된 주소를 읽어 크래시
```

크래시 로그에 `PC = 0x08001234`가 있으면 현재 실행 위치를 알 수 있다. 해당 펌웨어의 ELF 파일과 `addr2line`을 사용하면 `copy_field()`의 어느 줄인지 찾을 수 있다.

그런데 `copy_field()`가 여러 곳에서 호출된다면 “누가 어떤 경로로 호출했는가”가 필요하다. 이때 원하는 것이 다음과 같은 **backtrace**, 즉 현재까지 이어진 호출 경로다.

```text
copy_field → parse_response → download_file → main
```

**Unwind는 현재 함수의 상태에서 호출한 함수의 상태를 복원하는 과정**이다. backtrace는 그 과정을 반복하면서 얻은 주소 목록이다. “스택을 푼다”는 표현 때문에 메모리를 해제한다고 생각하기 쉽지만, 이 문서의 backtrace 수집은 스택을 읽어 가상의 상태를 계산하는 작업이다. 실제 함수에서 반환하거나 지역 변수를 파괴하는 C++ 예외 처리와는 구분한다.

### 2.2 스택에 호출 경로 목록이 그대로 들어 있는 것은 아니다

함수는 스택에 지역 변수, 보존할 레지스터, 반환 주소 등을 저장한다. 하지만 모든 함수가 같은 배치로 저장하지 않는다. 최적화 수준, 지역 변수 크기, 사용하는 레지스터 등에 따라 달라진다.

| 용어 | 쉬운 의미 |
| --- | --- |
| PC, Program Counter | 현재 실행 위치를 나타내는 값 |
| SP, Stack Pointer | 현재 스택 위치를 나타내는 값 |
| LR, Link Register | ARM 함수 호출 시 복귀 위치를 담는 레지스터. 중첩 호출 과정에서 값이 바뀌거나 스택에 저장됨 |
| FP, Frame Pointer | 함수의 스택 프레임을 찾기 위한 기준점. 설정·ABI에 따라 없을 수도 있음 |
| 스택 프레임 | 한 함수 실행에 관련된 스택 영역 |
| 레지스터 복원 | 호출되기 전 함수가 보던 값을 다시 계산하는 작업 |

LR 하나만 출력하면 전체 호출 경로가 되지 않는다. 현재 LR이 어느 호출의 복귀 주소인지, 더 바깥쪽 LR이 스택 어디에 저장됐는지를 알아야 한다.

### 2.3 언와인드 테이블은 “이 함수에서 한 단계 되돌아가는 설명서”다

컴파일러는 기계어를 만들면서 각 함수가 스택을 어떻게 사용하는지 알고 있다. 그 정보를 함께 남겨두면, 나중에 다음과 같은 복원 규칙을 읽을 수 있다.

```text
함수 copy_field의 복원 규칙 — 개념 예시
  1. 지역 변수에 쓰인 스택 공간 16바이트를 건너뛴다.
  2. 저장해 둔 r4 값을 읽는다.
  3. 저장해 둔 LR 값을 읽는다.
  4. 복원된 LR을 이용해 호출자 쪽으로 이동한다.
```

**테이블 자체에 과거 호출 이력이 저장되는 것은 아니다.** 테이블은 빌드할 때 만들어지는 정적인 설명서이고, 실제 호출 경로는 장애 순간의 레지스터와 스택에 있다. 둘을 함께 읽어야 한다.

```text
빌드할 때
  C/C++ 소스 → 컴파일러 → 기계어 + 스택 복원 규칙
                         ↓ 링커가 배치
                      최종 펌웨어

실행 중 또는 크래시 때
  현재 PC/SP/레지스터 + 스택 내용 + 복원 규칙
                         ↓ unwinder
                    호출 주소 목록
                         ↓ ELF와 addr2line
                    함수 이름·파일·줄 번호
```

스택이 이미 덮어써졌다면 설명서가 정확해도 잃어버린 반환 주소를 만들어낼 수 없다. 이는 테이블 기반 방식의 한계이지 테이블 생성 실패와 같은 문제가 아니다.

### 2.4 ARM에서 `.ARM.exidx`와 `.ARM.extab`은 무엇인가?

32비트 ARM EHABI에서 핵심은 다음 두 종류의 정보다.

| 항목 | 역할 | 비유 |
| --- | --- | --- |
| `.ARM.exidx` | 코드 주소로 복원 정보를 찾는 인덱스. 각 엔트리는 32비트 워드 2개, 총 8바이트 | 설명서의 목차 |
| `.ARM.extab` | 인덱스 안에 넣기 어려운 추가 복원 정보 등을 저장 | 상세 설명 페이지 |
| `EXIDX_CANTUNWIND` | 해당 엔트리에서는 복원할 수 없다는 표시 | 여기서 추적 중단 |
| `__exidx_start/end` | 링커가 제공하는 테이블 시작·끝 심볼 | 목차의 범위 |
| PREL31 | 현재 필드 위치에 대한 부호 있는 31비트 상대 주소 표현 | “여기에서 앞/뒤로 얼마”라는 위치 표현 |

단순한 복원 규칙은 `.ARM.exidx`의 두 번째 워드 안에 압축해서 들어간다. 더 긴 규칙은 `.ARM.extab`을 가리킨다. 테이블을 읽는 방식과 복원 명령은 [ARM EHABI 공식 명세][ehabi]에 정의되어 있다.

PREL31의 표현 범위는 약 ±1GiB이다. 코드와 테이블이 멀리 떨어진 메모리에 배치되면 링크 시 `R_ARM_PREL31` 범위 초과가 날 수 있다. 두 PR이 RAM에서 실행되는 커널 코드에 대한 테이블 생성 범위를 다르게 처리하는 배경이다. 이 범위는 [ARM ELF relocation 명세][aaelf]에서 확인할 수 있다.

### 2.5 테이블 방식, 프레임 포인터 방식, 스택 스캔 방식

| 방식 | 어떻게 찾는가 | 장점 | 한계 |
| --- | --- | --- | --- |
| 테이블 기반 | 컴파일러가 남긴 복원 규칙으로 이전 상태 계산 | 지원되는 코드에서 임의의 스택 값과 반환 주소를 구분할 수 있음 | 테이블·정확한 시작 상태·지원하는 명령 해석기가 필요 |
| 프레임 포인터 | 정해진 프레임 연결 구조를 따라감 | 구현이 비교적 단순하고 디버거와 함께 쓰기 쉬움 | 컴파일 설정과 함수 프레임 생성 방식에 의존 |
| 스택 스캔 | 스택의 값 중 코드 주소처럼 보이는 값을 후보로 선택 | 테이블 없는 구간에서도 단서 제공 가능 | 일반 데이터·이전 호출 흔적·함수 포인터가 섞여 오탐 가능 |

스택 스캔에서 후보 주소 앞의 `BL/BLX` 명령까지 확인하면 단순 주소 범위 검사보다 후보를 좁힐 수 있다. 그래도 **실제 활성 프레임이라는 증거와 같지는 않다.** 따라서 출력에도 `EHABI`, `FP`, `SCAN`처럼 근거를 구분하는 편이 좋다.

### 2.6 언와인드 테이블과 디버그 심볼은 다르다

- **언와인드 정보**: 이전 프레임의 상태를 계산하는 데 사용한다.
- **심볼·디버그 정보**: 주소를 함수 이름, 소스 파일, 줄 번호로 해석하는 데 사용한다.
- **스택 덤프**: 장애 당시 스택 메모리의 원시 값을 남긴다.

테이블이 있어도 보드 로그에는 주소만 나올 수 있다. 정확한 펌웨어와 일치하는 ELF를 보관해야 이름과 줄 번호를 해석할 수 있다. loadable app은 바이너리별 ELF 및 실제 로드 주소도 필요하다. 기존 [TizenRT 크래시 분석 문서][crashguide]도 이 주소 변환 절차를 설명한다.

### 2.7 컴파일 옵션을 어떻게 이해해야 하나?

| 옵션 | 이번 검토에서의 의미 |
| --- | --- |
| `-funwind-tables` | 스택 복원을 위한 정적 정보를 생성하도록 요청 |
| `-fexceptions` | 언어 차원의 예외 처리 지원. 순수 backtrace를 위해 항상 필요한 옵션은 아님 |
| `-fno-omit-frame-pointer` | 가능한 프레임 포인터 생성을 유지하도록 요청. 모든 함수의 완전한 연결을 무조건 보장하지는 않음 |
| `-fasynchronous-unwind-tables` | GCC가 지원 대상에서 명령어 경계 수준의 비동기 unwind 정보를 생성하는 옵션. ARM EHABI에 자동 적용되는 만능 해결책이 아님 |

옵션의 정확한 의미와 대상별 제약은 [GCC Code Generation Options][gcccodegen], [GCC Optimization Options][gccopt]를 기준으로 판단한다. “테이블만 켜면 실행 비용은 전혀 없다”도 과도한 표현이다. 테이블의 저장 공간, 실제 unwind 수행 시간, 함께 켠 옵션의 코드 생성 영향은 각각 측정해야 한다.

### 2.8 특히 크래시 분석에서 조심할 세 가지

1. **임의의 장애 PC와 정상 호출 지점은 다르다.** EHABI의 기본 예외 모델은 호출 지점을 중심으로 한다. 함수 진입·복귀 중간에 멈추면 스택 프레임이 완성되지 않았거나 이미 일부 해제됐을 수 있다. 테이블의 존재만으로 모든 fault PC의 복원 정확성을 보장하지 않는다.[EHABI 범위][ehabi]
2. **최적화로 사라진 함수는 물리적 프레임이 없다.** 인라인·tail call로 제거된 호출을 단순 backtrace만으로 원래 소스 호출 목록 그대로 복구할 수는 없다. 테스트에서 기대할 프레임도 최종 기계어와 비교해야 한다.[GCC 최적화][gccopt]
3. **CPU 예외 프레임은 일반 함수 프레임과 다르다.** 특히 Cortex-M은 예외 진입 시의 하드웨어 저장 프레임, MSP/PSP, EXC_RETURN 및 FPU 상태를 해석해야 한다. Cortex-A32용 연결 코드를 Cortex-M33에 그대로 적용했다고 지원이 완료되는 것은 아니다.[Cortex-M33 설명서][cm33], [레지스터와 보안 상태별 SP][cm33regs]

## 3. TizenRT에는 관련 기반이 일부 이미 있다

두 PR을 “아무 기반도 없는 TizenRT에 처음 테이블을 넣는 두 가지 방법”이라고 이해하면 정확하지 않다.

- RTL8730E의 기존 커널·XIP 사용자 링커 스크립트에는 `.ARM.extab`, `.ARM.exidx`, 시작·끝 심볼 배치가 있다.[커널 링커][7522-linker], [사용자 링커][7522-userlinker]
- 기존 ARMv7-A 코드에는 프레임 포인터/Thumb backtrace 구현이 있다.[기존 FP 구현][7522-oldfp]
- C++ 예외 처리를 위한 사용자 바이너리 exidx 검색·등록 경로도 존재한다. #7445는 이 환경에 의존하는 부분이 있고, #7522는 별도의 커널 진단용 등록 경로를 추가한다. 두 등록 경로를 같은 것으로 간주하면 안 된다.

새 기능에는 최소한 다음 네 요소가 맞물려야 한다.

```text
① 컴파일러의 테이블 생성
② 링커의 배치·보존
③ 로더가 실제 메모리 위치를 등록하고 수명을 관리
④ unwinder가 정확한 시작 문맥과 올바른 테이블로 해석
```

이 중 하나라도 빠지면 “빌드는 된다”, “심볼은 있다”, “일부 프레임이 찍힌다”와 “정확하게 사용할 수 있다” 사이에 차이가 생긴다.

<a id="pr7522-detail"></a>

## 4. PR #7522 상세 분석

### 4.1 의도와 변경의 중심

핵심 흐름은 다음과 같다.

```text
ASSERT / Data Abort
  → arm_assert.c의 상태 덤프
  → up_backtrace(fault_tcb, buffer, 32, 0, asserted_location)
  → 시작 문맥 선택: IRQ / 저장된 task 레지스터 / 현재 함수 상태
  → PC에 해당하는 kernel 또는 common/app exidx 찾기
  → unwind 명령을 해석하며 이전 프레임 복원
  → 주소와 kernel/common/app 이름 출력
```

일반 API 경로로는 `sched_backtrace(tid, buffer, size, skip)`도 추가한다. 그러나 ASSERT 출력은 이 scheduler wrapper가 아니라 `up_backtrace()`를 직접 호출한다.[ASSERT 연결][7522-assert], [scheduler wrapper][7522-sched]

### 4.2 설정·빌드 범위

| 설정/변경 | 실제 의미 |
| --- | --- |
| `CONFIG_SCHED_BACKTRACE` | ARM Kconfig에 추가하는 최상위 스위치. 기본 off |
| `CONFIG_UNWINDER_ARM` | 새 EHABI 엔진 선택 |
| `CONFIG_UNWINDER_FRAME_POINTER` | FP 방식 선택 및 `FRAME_POINTER` 선택 |
| `CONFIG_UNWINDER_STACK_POINTER` | 새 스택 스캔 구현 선택 |
| `CONFIG_ARM_UNWIND` | ARM 엔진 선택 시 켜지는 내부 bool |
| AMEBASMART 기본 선택 | backtrace를 켜면 ARM 엔진이 기본값 |
| 그 외 ARM의 기본 선택 | FP 엔진이 기본값. 해당 보드 연결이 완성됐다는 의미는 아님 |

`armv7-a/Toolchain.defs`는 ARM 엔진 선택 시 `-funwind-tables -fexceptions`를 `ARCHOPTIMIZATION`에 넣는다. RTL8730E `Make.defs`에도 C flags 추가가 있다. 실제 사용자·커널 C/C++에 최종적으로 전달되는 옵션은 빌드 명령으로 확인해야 한다.[설정][7522-kconfig], [toolchain][7522-toolchain], [보드 flags][7522-boardflags]

또한 `--no-merge-exidx-entries`를 ARMv7-A 링크 옵션에 **조건 없이** 추가한다. 이는 exidx 엔트리 병합을 제어하는 옵션이지, PREL31 표현 범위를 늘리거나 메모리 배치를 자동 수정하는 옵션이 아니다. 기능 off 구성도 링크 결과 비교 대상이다.[GNU ld ARM 옵션][ldarm]

### 4.3 직접 구현한 EHABI 해석기

`arm_backtrace_unwind.c`는 libgcc `_Unwind_Backtrace()`를 호출하지 않고 자체적으로 다음을 수행한다.[전체 엔진][7522-unwind]

1. **테이블 선택**: PC가 등록된 앱/common text 범위 안인지 확인한다. 아니면 커널 exidx를 검색한다.
2. **엔트리 검색**: PREL31 부호 구간을 찾는 `unwind_find_origin()`과 이진 탐색 성격의 `search_index()`로 엔트리를 찾는다.
3. **복원 규칙 선택**: 인덱스 안에 압축된 규칙 또는 외부 테이블 위치를 읽는다.
4. **가상 레지스터 집합 갱신**: `vrs[16]`에 대해 SP 증가/감소, 레지스터 pop, LR→PC 등을 수행한다.
5. **종료/제한**: CANTUNWIND, 미지원 명령, 스택 범위 이상, 진행 없는 PC/SP, 버퍼 한도 등에서 중단한다. 바깥 반복은 최대 `size × 4`회로 제한한다.

현재 decoder는 SP 조정, 코어 레지스터 pop, `vsp = rN`, finish, ULEB128 기반 큰 SP 조정, 일부 VFP 저장 영역 건너뛰기를 처리한다. VFP 값을 계산 목적으로 복원하기보다는 스택 위치를 이동하는 방식이다. 모든 EHABI personality와 확장 명령을 지원하는 범용 런타임으로 보아서는 안 된다.

`EXIDX_CANTUNWIND`를 만나면 코드는 중단하지만, 이것이 반드시 “정상적으로 최상위 프레임까지 도착했다”는 뜻은 아니다. 중간 함수가 복원 정보를 제공하지 않는 경우에도 똑같이 멈출 수 있으므로 종료 이유를 남기는 편이 유용하다.

### 4.4 커널·common·앱 테이블을 연결하는 방법

로드된 앱을 커널에서 분석하려면 커널의 `__exidx_start/end`만 알아서는 부족하다. 앱마다 코드와 exidx 범위가 다르기 때문이다.

PR은 다음 정보를 `g_app_exidx[]`에 저장한다.

```text
한 등록 항목
  exidx 시작 주소
  exidx 끝 주소
  text 시작 주소
  text 끝 주소
```

배열 용량은 `CONFIG_NUM_APPS + 1`이다. 추가 1칸은 common binary용이다. `up_register_exidx()`는 남은 칸에 append하며, 용량 초과 시 오류를 반환하지 않는다.[등록 구현][7522-register]

| 위치 | 추가 동작 |
| --- | --- |
| `userspace_s`, `binary_s` | C++ 예외 off라도 ARM 언와인더를 선택하면 exidx 시작·끝 필드 포함 |
| `up_userspace.c` | 사용자 바이너리 header에 링커 심볼 주소 게시 |
| `libxipelf/xipelf.c` | XIP 사용자 header에서 범위 읽기 |
| `libelf/libelf_load.c` | `.ARM.exidx` section을 찾아 runtime 범위 저장 |
| `binfmt_loadbinary.c` | common binary 로드 경로에서 등록 |
| `binfmt_execmodule.c` | 앱 실행 경로에서 등록 |

근거: [XIP loader][7522-xip], [ELF loader][7522-elfload], [common 등록][7522-loadbinary], [앱 등록][7522-exec].

이 변경은 loadable 환경을 다룬다는 점에서 #7445와의 큰 차이다. 동시에 사용자 header의 구성 조건이 바뀌므로 커널·common·앱이 같은 header/설정 계약을 사용해야 한다. 해당 구성에서 기존 바이너리와 부분적으로 섞어 배포하는 것은 별도 호환성 검토 대상이다.

### 4.5 시작 문맥은 어떻게 선택하나?

| 상황 | 현재 코드의 처리 |
| --- | --- |
| 현재 task + IRQ/exception 문맥 | IRQ 쪽 프레임을 먼저 시도하고, 여유가 있으면 `CURRENT_REGS`로 중단된 task 쪽 복원을 시도 |
| 현재 task + 저장된 `xcp.regs` 있음 | 저장된 FP/SP/LR/PC 사용. 일부 ASSERT 조건에서는 아래 스캔 경로 사용 |
| 현재 task + 저장된 문맥 없음 | builtin/inline assembly로 현재 프레임 관련 값을 수집 |
| 다른 task | 그 task의 `xcp.regs` 사용 |
| 다른 CPU에서 실행 중인 task | scheduler wrapper가 IPI 미지원 메시지를 남기고 0 반환 |

근거: [up_backtrace 문맥 선택][7522-context], [다른 CPU 처리][7522-sched].

따라서 “모든 task를 언제든 안전하게 추적한다”는 API는 아니다. 다른 task의 생존과 정지 상태를 유지하는 동기화도 별도로 필요하다. wrapper는 `sched_gettcb()` 후 대상이 분석 중 종료·재스케줄되지 않도록 보호하는 절차를 추가하지 않는다.

### 4.6 사용자 ASSERT에는 테이블 해석이 아닌 스캔 경로가 있다

이 PR에서 가장 오해하기 쉬운 부분이다.

- `arm_syscall.c`는 `SYS_up_assert`에서 사용자 LR, SP 및 레지스터 포인터를 저장한다.
- `up_backtrace()`의 APP_BINARY_SEPARATION 경로에는 `asserted_location != 0 && asserted_location < 0x60000000`이면 사용자 SP부터 스택을 훑는 분기가 있다.
- 스택 값이 `0x08000000 ≤ value < 0x20000000`이면 코드 주소 후보로 채택한다.
- 중복 주소를 제거하고 **최대 10개**까지만 기록한다. `skip`도 이 경로의 프레임 선택에 반영하지 않는다.
- 이 경로는 EHABI 명령 해석이나 BL/BLX 검증을 하지 않는다.[실제 분기][7522-assertscan]

그런데 ASSERT 출력은 모든 프레임에 `[EHABI]`를 붙인다. 따라서 **현재 로그의 `[EHABI]`만 보고 그 프레임이 테이블로 증명된 호출 경로라고 판단할 수 없다.** 엔진 선택이 SP/FP일 때도 출력 문구는 EHABI로 고정되어 있다.[출력 코드][7522-assert]

주소 범위의 상수도 RTL8730E 배치에 치우친 가정이다. 재귀처럼 같은 함수 주소가 실제로 반복되는 호출은 중복 제거 때문에 원래 깊이를 잃을 수 있다.

### 4.7 RAM 코드의 테이블을 끄는 이유와 대가

RTL8730E는 Flash에서 직접 실행하는 코드와 RAM에서 실행해야 하는 코드가 떨어져 있다. 기존 링커는 `.sramdram.only.text`, MMU·CPU pause·spinlock 관련 코드를 RAM 구간에 놓고 exidx/extab은 XIP 구간에 놓는다.[링커 배치][7522-linker]

PR은 PREL31 범위 초과를 피하려고 다음 파일들에 `no-unwind-tables`, `no-exceptions` pragma를 추가한다.

- 부트, CPU pause, Data Abort, MMU 처리
- IRQ spinlock 및 semaphore spinlock
- FTL, FTL crypto, flash API, Flash RAM 함수
- RTL8720E의 Flash RAM 소스 한 곳도 포함

**링크를 가능하게 하는 대신 추적할 수 없는 구간을 만드는 선택**이다. 예를 들어 lock 관련 장애를 조사할 때 필요한 함수가 제외 대상이면 바로 그 지점에서 trace가 끊길 수 있다. “완전한 backtrace”라는 PR 설명은 이 제외 범위와 함께 읽어야 한다.

더 넓은 커버리지가 필요하면 메모리 영역별 테이블 배치·조회 설계와 linker map을 함께 검토해야 한다. 단순히 모든 제외 pragma를 제거하라는 권고는 아니다.

### 4.8 도입 전 반드시 해결할 문제

아래는 현재 head의 소스 및 규격에서 확인한 문제다. 보드에서 이미 발생했다고 주장하는 목록은 아니다.

| 우선순위 | 문제 | 근거와 영향 | 필요한 조치 |
| --- | --- | --- | --- |
| 높음 | IRQ 스택 경계에 실제 스택 대신 포인터 배열 주소 사용 | `frame.stack_base = (unsigned long)g_irqstack_top`. 실제 심볼은 CPU별 스택 끝 주소의 배열이다. 경계가 엉뚱한 영역을 가리킨다 | CPU별 실제 stack bottom/top API 사용. SMP/UP 각각 검증 |
| 높음 | 앱 등록에 삭제·교체·중복 처리 없음 | append-only이며 용량은 동시 앱 수+1. 재시작을 반복하면 이전 범위가 남고 신규 등록은 조용히 누락될 수 있음 | binary ID와 수명에 연결해 load/unload/reload를 원자적으로 관리 |
| 높음 | 앱 실행 이후 테이블 등록 | `task_activate()`가 등록보다 먼저다. 새 task가 먼저 실행되면 시작 직후 fault를 등록 전에 만날 수 있음 | 실행 가능 상태로 전환하기 전에 등록, 실패 시 rollback |
| 높음 | 가상 레지스터 초기화·보존이 불완전 | `vrs[16]` 중 FP/SP/LR/PC만 초기화. `vsp=rN`은 다른 레지스터도 참조할 수 있고, 프레임 간 보존 상태도 네 값 위주 | 필요한 callee-saved 상태를 캡처·보존하고 미확보 상태는 명시적으로 중단 |
| 높음 | 잘못된 초기 SP로 첫 메모리 읽기를 할 수 있음 | pop 전 검사는 조건부 상한 검사이며 초기 하한·정렬 검사가 없다. 범위 검사가 명령 실행 뒤에 수행됨 | 각 읽기 전에 overflow·하한·상한·정렬 검증. table 범위도 검증 |
| 높음 | FP/SP 선택 구성이 EHABI 구현과 충돌 | EHABI 파일은 무조건 빌드되고 SP 파일도 동일한 `up_backtrace`를 정의. 공통 헤더는 5인자, SP 구현은 4인자. FP 선택 경로의 `common/arm_backtrace_fp.c`는 해당 head에 없음 | 선택한 엔진만 빌드하고 동일한 API로 통일. 모든 선택 조합 빌드 |
| 높음 | 스캔 주소에도 `[EHABI]` 표시 | ASSERT 스캔 분기와 고정 출력 문구의 조합 | 프레임별 수집 방식과 실패 이유 전달 |
| 중간 이상 | 일부 정상 EHABI 표현을 잘못 처리 | personality/압축 형식 지원이 제한적이고 VFP operand 0을 거부하는 경로가 있음 | 지원 subset 명시, 미지원 형식은 안전하게 거절, ABI 테스트 벡터 추가 |
| 중간 이상 | raw PC와 반환 주소를 동일하게 보정 | 초기 PC·LR와 복원 주소 모두 `(addr & ~1) - 2`로 저장 | fault PC는 원본 보존. 반환 주소의 call-site 해석은 ISA·도구 목적별 분리 |

주요 근거: [IRQ 문맥 코드][7522-context]와 [실제 IRQ 배열/API][7522-irq]; [등록 배열][7522-register]와 [task 활성화 순서][7522-exec]; [VRS·메모리 접근][7522-frame]; [opcode 처리][7522-opcodes]; [빌드 선택][7522-make], [SP 함수 시그니처][7522-sp], [공통 선언][7522-archh].

#### IRQ 경계 오류를 쉽게 풀어 쓰면

`g_irqstack_top`은 “스택 끝 주소가 적힌 목록의 위치”다. `g_irqstack_top[cpu]`는 “그 목록에서 꺼낸 실제 스택 끝 주소”다. 두 값은 다르다. 기존 코드에 이미 `arm_intstack_top()`과 `arm_intstack_alloc()`이 있으므로 이 계약을 활용하는 것이 자연스럽다.

#### 앱 재시작 문제를 쉽게 풀어 쓰면

동시에 앱 2개와 common 1개가 있다고 가정하면 등록 칸은 3개다. 처음 부팅 후 3칸이 찬다. 앱 하나를 재시작해도 이전 칸을 교체하지 않으므로 새 테이블이 등록되지 않을 수 있다. RAM에 로드한 이전 이미지가 해제되면 오래된 메모리를 조회할 위험도 생긴다. 이는 배열과 등록 함수로부터 도출한 실행 시나리오이며, 실제 recovery 반복 실험은 필요하다.

#### decoder에서 더 확인할 부분

`unwind_frame()`은 compact 0x80/0x81 외의 header를 일반 명령 바이트처럼 처리하는 경로가 있다. 다른 compact personality나 generic personality header를 그렇게 읽어도 된다는 보장은 없다. 외부 테이블 포인터는 정렬만 검사하고 유효 범위 검사는 하지 않는다. `unwind_get_byte()`는 입력 고갈을 0으로 돌려줘 잘린 입력과 정상 opcode를 구별하지 못한다.[frame 파서][7522-frame], [byte/opcode 파서][7522-opcodes]

규격 대조로 확인한 구체적인 예는 다음과 같다. 실제 타깃 이미지가 이 표현을 사용하는지는 `readelf --unwind`로 확인해야 한다.

- compact personality 2의 `0x82` header를 별도 처리하지 않아 header부터 pop 명령처럼 읽는 경로가 있다.
- generic extab의 첫 워드는 personality 함수의 상대 주소다. 이를 그대로 unwind opcode로 처리해서는 안 된다.
- `B3/C8/C9`의 다음 바이트는 시작 레지스터와 개수−1을 담는다. 코드의 `mask == 0` 거절과 달리, `B3 00`, `C9 00`은 D0 하나를 나타내는 유효한 표현이다. `C8 00`도 D16이 존재하는 아키텍처에서는 유효하다.[EHABI 명령 형식][ehabi]

현재 프레임 수집에도 주의가 필요하다. FP 경로에서 함수 진입 주소·현재 FP/SP·LR를 섞고, 비-FP 경로에서는 현재 함수의 SP와 호출자의 반환 PC를 조합하는 지점이 있다. EHABI가 해석할 함수와 SP 상태가 같은 순간의 것이어야 하므로, 최적화별 assembly 확인과 정확한 context capture가 필요하다.[현재 문맥 초기화][7522-context]

### 4.9 테스트 예제와 공개 로그의 의미

새 `backtrace_test` 예제는 세 종류를 제공한다.

| 명령 | 실제 트리거 |
| --- | --- |
| `backtrace_test 0` | 3단계 함수 호출 후 NULL write로 Data Abort |
| `backtrace_test 1` | 10단계 함수 호출 후 NULL write로 Data Abort |
| `backtrace_test 2` | `DEBUGASSERT(0)` |

주석 일부는 ASSERT라고 표현하지만 0·1번의 실제 코드는 NULL write다. 10단계 함수에는 `noinline`이 있으나 그것만으로 tail-call 제거까지 막지는 않는다. 2번은 DEBUGASSERT 활성화 여부도 확인해야 한다.[예제 소스][7522-tests]

PR 본문의 로그는 깊은 호출 경로와 kernel/common 분류가 실제로 출력된 사례를 보여준다. 다만 동일 주소가 중복 출력된 예도 있고, 기대 프레임과 기계어를 자동 비교하는 테스트는 포함되어 있지 않다. 이 증거는 “일부 실행 경로에서 유용한 로그를 얻었다”는 수준으로 평가한다.

### 4.10 #7522 평가

장점은 크래시 분석의 핵심 문제를 직접 다루고, 커널과 loadable binary를 연결할 구조를 마련한다는 점이다. 반면 자체 ABI 파서, fault/IRQ 문맥, loader 수명 관리까지 프로젝트가 유지해야 한다. 수정 범위와 검증 책임이 상당하다.

**도입 가치는 높지만, 현재 head는 검증을 마친 범용 ARM 크래시 언와인더로 취급하기 어렵다.** 특히 현재 기능이 필요할 법한 MM·spinlock 장애 조사에서는 테이블 제외 구간과 fault 문맥 정확성을 먼저 확인해야 한다.

<a id="pr7445-detail"></a>

## 5. PR #7445 상세 분석

### 5.1 이 PR은 무엇을 알고 싶은가?

예를 들어 화면 진입 전후로 heap 사용량이 계속 늘어난다고 하자. 기존 heap 총량만 보면 “얼마나 늘었다”는 알 수 있지만, 어떤 코드 경로가 만든 블록인지 찾기 어렵다.

#7445는 측정 구간을 열고 닫는 기능을 추가한다.

```text
capture start
  → 의심되는 동작 실행: 화면 진입, 연결, 요청 처리 등
  → malloc / realloc / free 이벤트를 heapinfo 집계와 함께 기록
capture stop
  → 구간 중 새로 할당되어 남은 블록
  → 구간 이전 블록의 크기 변경
  → 구간 이전 블록의 해제
  → 각 블록의 주소·크기·소유 PID·저장된 할당 경로 출력
```

**남아 있는 블록은 누수 후보이지 곧바로 확정 누수는 아니다.** 캐시, 정상적인 수명 연장, 비동기 작업이 끝나기 전의 버퍼도 남을 수 있다. 반대로 시작 이전에 이미 누수된 블록은 “구간 중 새 할당” 목록에 나오지 않는다.

### 5.2 새 사용자 명령

코드상 지원하는 예시는 다음과 같다. `app1`, PID `30`은 예시 값이다. 현재 구현의 집계 범위 문제를 피하려면 start/stop에 같은 대상과 PID를 명시해야 한다. 이 문서에서 명령을 실행한 것은 아니다.[명령 구현][7445-cli]

```sh
# app1 heap에서 PID 30의 구간 기록
heapinfo -b app1 -p 30 -c start
# ... 조사할 작업 수행 ...
heapinfo -b app1 -p 30 -c stop

# 현재 살아 있는 할당에 저장된 호출 경로 출력
heapinfo -b app1 -p 30 -t

# 수집 시 안쪽 몇 프레임을 건너뛸지 변경. 허용 범위 0..32
heapinfo -s 3
```

`-b`는 binary heap 선택, `-k`는 kernel heap 선택, `-p`는 PID 선택이다. `-t`는 호출 스택을 그 순간 새로 복원하는 명령이 아니라 **각 할당 노드에 이미 기록한 주소를 읽는 명령**이다. `-s`는 저장 깊이를 바꾸는 옵션이 아니라 allocator wrapper 등을 건너뛰는 프레임 수를 바꾼다.

자동으로 N초 뒤 멈추는 타이머는 추가하지 않는다. 여기서 시간 구간은 사용자가 start/stop으로 정한다. 기본 대상은 kernel heap이며 capture 명령은 모든 앱을 자동 순회하지 않으므로 앱 조사에는 `-b`를 명시한다.

### 5.3 사용자 영역 unwinder: libgcc 재사용

새 파일 `lib/libc/sched/sched_backtrace.c`는 다음과 같이 동작한다.[libc 구현][7445-libc]

1. `buffer`, `size`를 검사한다. 잘못된 입력에는 0을 반환한다.
2. 음수 `skip`은 0으로 정규화한다.
3. `tid != getpid()`이면 0을 반환한다. **현재 호출 중인 thread만 지원**한다.
4. `_Unwind_Backtrace(backtrace_helper, &arg)`를 호출한다.
5. callback에서 `_Unwind_GetIP()`로 프레임 주소를 얻는다.
6. IP와 CFA가 모두 같아 진행이 없으면 중단한다. CFA는 프레임의 기준 스택 주소에 해당하는 값이다.
7. 내부 프레임·사용자 지정 skip을 제외하고 버퍼 한도까지 저장한다. 끝의 NULL 항목을 일부 정리한다.

ABI opcode를 자체적으로 구현하지 않는다는 점은 유지보수상 이점이다. 그렇다고 임의의 망가진 task 스택이나 fault context까지 안전하다는 의미는 아니다. 이 API는 **정상적인 호출 흐름에서 자기 스택을 걷는 용도**다. ASSERT integration, 중단된 다른 task, 원격 CPU, 사용자/커널 전환을 가로지르는 crash trace를 추가하지 않는다.

`unwind_arch_adjustment()`라는 weak hook이 있지만 PR의 기본 구현은 주소를 그대로 반환한다. #7522처럼 모든 주소에서 2를 빼는 방식과 주소 의미가 다르므로 결과를 합칠 때 주의해야 한다.

### 5.4 테이블 생성은 사용자 영역으로 제한

`CONFIG_SCHED_BACKTRACE=y`여도 새 `-funwind-tables` 옵션은 다음 조건에서만 추가된다.

```text
CONFIG_BUILD_FLAT != y
그리고 EXTRADEFINES에 __KERNEL__이 없음
```

즉, two-pass 빌드의 사용자 pass를 겨냥한다. RAM 커널 코드에 테이블을 생성하면 PREL31 overflow가 날 수 있어 제외한다. C와 C++ flags 모두에 옵션을 더하지만, 별도로 모든 커널 코드를 테이블화하지는 않는다.[toolchain 조건][7445-toolchain], [보드 조건][7445-boardflags]

flat 구성에서는 이 PR이 자동으로 테이블 생성 옵션을 넣지 않는다. 기존 다른 설정이 필요한 정보를 만들어줄 가능성과 별개로, **`SCHED_BACKTRACE=y`만으로 flat 지원이 완성된다고 볼 수 없다.**

또 하나의 조건은 바이너리 테이블 조회다. 기존 `gnu_unwind_find_exidx.c`와 XIP loader의 등록 기반이 있지만, 사용자 header 게시와 앱 등록은 `CONFIG_LIBCXX_EXCEPTION` 아래에 있다. #7445의 `SCHED_BACKTRACE`는 이를 의존 조건으로 선언하거나 독립적인 등록 경로를 추가하지 않는다. 수정한 예시 defconfig는 `LIBCXX_EXCEPTION=y`이므로 이 기반을 사용할 수 있지만, C++ 예외 off 환경에서는 추가 연결이 필요하다.[기존 lookup][7445-exidx], [사용자 header 게시][7445-userspace], [기존 등록][7445-exec]

### 5.5 “사용자 영역만 수집한다”는 설명과 실제 매크로의 차이

`MM_ADD_BACKTRACE(node)`는 컴파일되는 영역에 따라 엔진을 고른다.

| 구성 | 호출하는 엔진 |
| --- | --- |
| `SCHED_BACKTRACE=y`, `__KERNEL__` 없음 | libc `sched_backtrace(getpid(), ...)` → libgcc |
| `SCHED_BACKTRACE=y`, `__KERNEL__` 있음 | 아키텍처 `up_backtrace(NULL, ...)` |
| `SCHED_BACKTRACE` off | 아키텍처 `up_backtrace()` 또는 weak no-op stub |

이 매크로에는 capture active나 대상 PID를 검사하는 조건이 없다. `mm_malloc`, `mm_memalign`, `mm_realloc`이 호출하는 위치에서 실행된다. **캡처 테이블에 이벤트를 넣는 필터와 호출 스택 수집을 실행할지 결정하는 필터가 분리되어 있고, 후자는 현재 코드에 없다.**[MM 매크로][7445-mm-macro], [malloc 연결][7445-malloc]

따라서 PR 본문의 “대상 task에만 캡처”, “사용자 할당으로 제한”은 구간 이벤트 선택에 대한 의도로 읽어야 한다. 최신 코드가 실제로 커널·다른 task의 backtrace 시도까지 모두 막아준다는 의미로 사용하면 안 된다. early boot·IDLE·IRQ 안전성도 별도 검증 대상이다.

### 5.6 heap node와 heap 자체의 변경

기존의 `alloc_call_addr` 단일 주소를 제거하고 `backtrace[]` 배열을 통합 필드로 사용한다. 일부 기존 “호출자 주소” 사용 지점은 `backtrace[0]`으로 바뀐다. 옵션에 따라 `seqno`도 추가한다.[노드 구조체][7445-mm-node]

```text
mm_allocnode_s
  preceding / PID / memory_state / size
  [선택] seqno
  [선택] backtrace[N]

mm_heap_s
  기존 allocator 상태
  capture active
  capture PID
  capture table 포인터 / 엔트리 수 / 유실 수
  시작 시점의 heapinfo 사용량 등
```

free node에도 reserved fields가 늘고, backtrace 기능이 켜졌을 때 구조체를 `MM_MIN_CHUNK` 단위로 정렬한다. 이는 단순 로그 추가를 넘어 **allocator header와 free-list의 크기 계약을 바꾸는 변경**이다. kernel/common/app이 같은 layout을 사용해야 하며, 이전 binary와 섞는 구성은 검증 없이 허용할 수 없다.

### 5.7 설정의 의도와 실제 동작

| 옵션 | Kconfig 설명 | 현재 head에서 주의할 점 |
| --- | --- | --- |
| `MM_BACKTRACE=-1` | 기능 off | HEAPINFO가 켜져 있으면 header에서 3으로 강제 변경 |
| `MM_BACKTRACE=0` | sequence만 저장 | HEAPINFO가 켜져 있으면 역시 3으로 변경 |
| `MM_BACKTRACE=N>0` | N개 주소 저장 | N=1·2도 허용하지만 일부 출력은 2·3칸을 고정 참조 |
| `MM_BACKTRACE_SEQNO` | sequence 기록, 기본 y | 유효 조건 안에서만 필드와 증가 코드 생성 |
| `MM_BACKTRACE_DEFAULT` | 시작 시 기본 활성화 여부 | 수집 매크로에서 이 옵션으로 실행을 제어하지 않음 |
| `MM_BACKTRACE_SKIP` | 기본 3개 프레임 제외 | runtime `g_mm_backtrace_skip`으로 조정 |
| `ARCH_HAVE_BACKTRACE` | 동작하는 arch 구현의 존재 | 선언만으로 모든 보드가 구현을 제공하는 것은 아님 |

근거: [MM Kconfig][7445-mm-kconfig], [header의 강제 재정의][7445-mm-force], [고정 인덱스 출력][7445-parse].

수정된 `loadable_ext_ddr_st7785/defconfig`에서 backtrace 관련 네 줄은 `#CONFIG_...` 형태의 **주석**이다. 실제로 켜는 변경은 `CONFIG_FRAME_POINTER=y`이다. 이 파일 변경만 보고 EHABI와 할당 backtrace가 CI에서 활성화됐다고 판단해서는 안 된다.[주석 네 줄][7445-defconfig], [실제 FP 설정][7445-defconfig-fp]

### 5.8 capture window 내부 상태 전이

#### 시작

`heapinfo_capture_start()`는 이전 table을 분리·해제하고, 대상 heap에서 기본 1,024개 엔트리 배열을 할당한다. 그다음 대상 PID, 엔트리 수·유실 수, 시작 사용량을 기록하고 active를 켠다. table 자체의 할당은 active를 켜기 전에 수행하여 구간 이벤트로 넣지 않는다.[start 구현][7445-capture]

상태를 전역 변수 하나가 아니라 `mm_heap_s` 안에 둔 것은 protected/loadable 구성에서 커널 ioctl과 사용자 allocator가 같은 heap 상태를 보게 하려는 설계다. 다중 heap이면 각 heap에 별도 table이 생길 수 있다. 여러 heap의 start/stop은 driver가 순차 처리하므로 시스템 전체의 완전히 같은 순간을 찍는 snapshot은 아니다.[driver][7445-driver]

#### 기록

`heapinfo_add_size()`와 `heapinfo_subtract_size()`에서 기존 PID별 사용량 갱신과 함께 table을 갱신한다. 주소, allocator 노드 크기, PID, 호출자 및 backtrace 사본을 저장한다.

- 구간 중 할당 후 살아 있으면 `ALLOC`.
- 구간 중 할당 후 해제되면 해당 항목 제거.
- 구간 이전 블록이 구간 중 해제되면 `FREED` 이력.
- 기존 블록을 재할당하면 free/alloc 항목을 찾아 `REALLOC`로 합치는 방식.
- table이 차면 추가 기록을 생략하고 `lost` 증가.

주소 재사용을 고려해 뒤에서부터 일치 항목을 찾고, FREED 항목은 살아 있는 allocation 검색에서 제외한다. 삭제 시 마지막 항목으로 채우므로 출력 순서는 시간순이 아니다. table의 “1,024개”는 무제한 이벤트 history도, 모든 현재 allocation 수용 보장도 아니다.[이벤트 처리][7445-capture]

#### 종료와 출력

`heapinfo_capture_stop()`은 active를 끈다. 별도 `heapinfo_capture_report()`가 table을 heap에서 분리하고 종료 사용량을 읽은 뒤 lock 밖에서 출력하고 table을 해제한다.[stop][7445-capture], [report][7445-report]

출력을 lock 밖에서 하는 방향은 좋다. 그러나 **중단과 table/counter snapshot이 하나의 원자적 동작으로 묶이지 않은 점**은 아래 집계 문제를 만든다.

### 5.9 세 출력 영역과 집계식

| 출력 영역 | 의미 | 사용량 변화에 더하는 값 |
| --- | --- | --- |
| `[1] NEW ALLOCATIONS` | 구간 안에서 생성되고 종료 때까지 남은 블록 | `+현재 크기` |
| `[2] REALLOCATIONS` | 구간 전부터 있던 블록의 크기 변경 | `+새 크기 − 기존 크기` |
| `[3] FREED` | 구간 전부터 있던 블록의 해제 | `−기존 크기` |

의도한 관계는 다음과 같다.

```text
구간 순증가 = 새 할당 합계 + 재할당 크기 차이 합계 − 이전 블록 해제 합계
          = 종료 사용량 − 시작 사용량
```

예를 들어 새로 남은 블록이 96바이트, 기존 블록이 64→112바이트, 다른 기존 블록 64바이트가 해제됐다면 순증가는 `96 + 48 − 64 = 80바이트`다. 이는 **allocator의 node size 단위로 만든 설명용 예시**다. `malloc()`에 요청한 payload 크기만을 뜻하지 않는다. header와 정렬이 실제 사용량에 포함된다.[엔트리 크기와 출력][7445-report]

PR 본문은 집계식이 정확히 맞는다고 설명하지만, 현재 코드에서는 다음 절의 조건 때문에 항상 성립하지 않는다. 특히 유실이 발생한 table의 합계는 전체 counter delta와 같다고 보장할 수 없다.

주소도 구별해야 한다. capture 표의 `MemAddr`는 allocator **node 주소**이고 `-t`의 `ptr`는 header 뒤의 **사용자 반환 주소**다. 두 출력의 차이를 블록이 이동한 것으로 오해하면 안 된다. 기존 `mem_leak_checker`가 LEAK로 분류한 노드에도 저장된 backtrace를 추가 출력하지만, 그 검사의 분류와 capture window의 “아직 살아 있음”은 같은 판정이 아니다.[현재 할당 출력][7445-dump], [memleak 변경][7445-memleak]

### 5.10 도입 전 반드시 해결할 문제

#### A. 호출 스택을 완성하기 전에 capture table에 복사한다

일반 malloc의 핵심 순서는 다음과 같다.[malloc][7445-malloc], [caller/accounting 갱신][7445-capture], [기록 매크로][7445-mm-macro]

```text
노드의 caller/PID 등 설정
  → heapinfo_add_size()
      → heapinfo_capture_insert()
          → 현재 node->backtrace[]를 table에 memcpy
  → MM_ADD_BACKTRACE(node)
      → 이제 실제 새 호출 스택을 node에 기록
```

따라서 table에는 **새 스택 수집 전의 배열**이 복사된다. 첫 caller 값이 먼저 설정되더라도 더 깊은 주소들은 이전 내용이나 초기화되지 않은 값일 수 있다. 이후 node에 올바른 스택을 써도 table 사본은 갱신되지 않는다. memalign/realloc 연결도 함께 수정해야 한다.

이는 PR의 핵심 가치인 “이 할당을 만든 정확한 경로”에 직접 영향을 준다. 메타데이터 완성, counter 갱신, event snapshot의 순서를 명확한 하나의 계약으로 만들어야 한다.

#### B. 기존 1단계 caller가 NULL로 사라질 수 있다

HEAPINFO가 켜져 있고 설정된 backtrace 깊이가 0 이하이면 3으로 강제 변경된다. 양수 1·2는 그대로 유지된다. 그런데 `SCHED_BACKTRACE` off 구성에서 weak stub이 사용되면 0프레임을 반환하면서 `buffer[0] = NULL`을 쓴다. 이는 앞서 저장한 기존 caller 값을 덮는다.[강제 활성화][7445-mm-force], [weak stub][7445-stub]

**backtrace 수집 실패가 기존 단일 caller 정보까지 잃게 만드는 회귀**다. immediate caller를 보존하고, 유효 frame count와 실패 사유를 분리하는 방향이 필요하다. “backtrace off이면 기존과 같다”는 설명도 현재 코드에는 맞지 않는다.

#### C. stop 경계와 counter snapshot 사이에 이벤트가 끼어들 수 있다

코드상 가능한 순서의 예다. 실제 보드에서 실행한 재현 결과는 아니다.

```text
Task A: stop → active=false → unlock
Task B: malloc → counter 증가, active=false라 table에는 미기록
Task A: report → table 분리 + 증가한 counter snapshot
```

그러면 table 합계와 종료 counter delta가 달라진다. free 쪽은 table이 있으면 active와 무관하게 기존 항목 제거를 시도하는 경로도 있어, stop 이후에도 table 내용이 바뀔 수 있다.[stop·subtract 구현][7445-capture], [report snapshot][7445-report]

active off, table detach, 종료 counter, 필터 정보의 snapshot을 **한 lock 구간**에서 확정하고 출력은 그 다음에 해야 한다.

#### D. 시작 PID와 종료 PID가 다르면 서로 다른 값을 뺀다

start는 `mm_capture_pid`를 저장하지만 report는 저장된 값을 기준으로 종료 snapshot을 읽지 않고 **stop 명령에서 전달된 PID**를 사용한다. start에 `-p 30`, stop에 `-p` 생략이면 “시작 PID 30 사용량”과 “종료 전체 heap 사용량”을 비교할 수 있다.[start][7445-capture], [report][7445-report]

필터는 session의 일부로 유지하고, stop에서는 저장된 필터를 사용하거나 불일치를 오류로 처리해야 한다.

#### E. 작은 depth 설정과 고정 인덱스 접근

Kconfig는 양의 깊이 1·2를 막지 않는다. 그런데 일반 heap 상세 출력은 `backtrace[1]`, `[2]`를 고정 참조하고 capture report도 두 칸을 고정 출력한다. 짧은 배열 설정에서는 범위 밖 읽기가 가능하다.[설정][7445-mm-kconfig], [상세 출력][7445-parse], [capture 출력][7445-report]

또한 `-t` 출력은 깊이만큼 순회하지만 capture stop 출력은 owner 및 고정 두 칸 위주다. 본문의 “full call backtrace”가 모든 출력 경로에서 임의 깊이를 전부 표시한다는 뜻으로 구현되어 있지 않다. 모든 출력이 실제 수집된 길이를 사용해야 한다.

#### F. 실행 비용과 수명·동기화 계약

| 항목 | 소스에서 확인한 경계 또는 추가 검증 |
| --- | --- |
| 정상 할당 지연 | capture/PID와 무관한 backtrace 시도가 있고, allocator lock 안에서 수행되는 위치가 있음. 최악 지연 측정 필요 |
| table 검색 | free/realloc에서 선형 검색. 이벤트가 많으면 lock 보유 시간 증가 가능 |
| sequence | 전역 `++g_mm_seqno`. 서로 다른 heap의 lock은 전역 증가를 직렬화하지 않으므로 SMP 전역 고유 순서로 해석 불가 |
| start 실패 | table malloc 실패는 메시지 후 void 반환. driver는 capture 분기에서 OK로 처리하므로 명령 성공과 측정 시작을 구별하기 어려움 |
| 중복 start/stop | 여러 호출자 간 session 소유권·수명 계약 검증 필요 |
| skip 변경 | 사용자 사본 직접 변경 + kernel ioctl. ioctl 실패를 최종 성공 메시지에 반영하지 않음 |
| 앱 종료·PID 재사용 | PID만으로 구간 identity를 계속 식별할 수 있는지, heap unload와 출력이 겹쳐도 안전한지 검증 필요 |

근거: [MM 매크로와 sequence][7445-mm-macro], [table 관리][7445-capture], [driver 결과 처리][7445-driver], [skip 명령][7445-cli].

#### G. 구성 의존성과 남은 집계 경계

- **커널 엔진 provider 누락**: `SCHED_BACKTRACE=y`이면 커널 MM은 `up_backtrace()`를 호출하고 weak stub은 빌드되지 않는다. 그런데 AMEBASMART의 실제 엔진 빌드는 `ARCH_HAVE_BACKTRACE`에 달렸고 새 capability의 기본값은 off다. 두 설정을 연결하지 않은 조합은 provider가 빠지는 구성이므로 정리해야 한다. 이는 정적 구성 분석이며 전체 target link 실패를 실행으로 확인한 것은 아니다.[arch 빌드][7445-make], [stub 조건][7445-stub]
- **HEAPINFO off 조합**: `MM_BACKTRACE>0`이면 driver의 skip 처리 코드가 들어가지만, 그 코드가 쓰는 `HEAPINFO_BACKTRACE_SKIP_MAX`는 HEAPINFO 블록 안에서만 정의된다. 기능 의존성을 맞추거나 공통 상수를 적절히 이동해야 한다.[정의 범위][7445-mm-force], [사용 범위][7445-driver]
- **전체 PID 집계와 task stack 생성**: 전체 창의 기준은 `total_alloc_size`인데 table은 per-PID 집계 경로를 따른다. task stack을 per-PID 사용량에서 제외하는 경로는 table에서도 제거하지만 global total을 같은 방식으로 빼지 않는다. 창 안에서 task를 생성하는 경우까지 같은 집계식이 맞는지 별도로 검증해야 한다.[stack 제외 경로][7445-stackexclude]
- **주소 재사용 후 realloc**: realloc 합침은 주소를 기준으로 이전 FREED와 새 ALLOC를 연결한다. 오래된 A를 free하고 같은 주소에 새 B를 만든 뒤 realloc하면 서로 다른 allocation을 연결할 가능성이 있다. 합계뿐 아니라 NEW/REALLOC 분류도 검증하고, 필요하면 allocation identity를 함께 기록해야 한다.[합침 코드][7445-capture]

`heapinfo_capture_note_realloc()`은 자체적으로 heap lock을 잡는다. 기존 MM은 같은 PID의 재귀 획득을 허용하므로 중첩 호출 자체를 즉시 deadlock이라고 판정하지 않는다. 다만 이 저장소에서 검토 중인 [MM 비재귀 잠금 전환 계획](MM_Nonrecursive_Locking_Plan.md)과 함께 도입한다면, 새 재귀 획득 경로도 전환 대상에 넣어야 한다.[해당 PR의 MM lock][7445-sem]

### 5.11 #7445 평가

이 PR의 강점은 메모리 사용량 증가를 실제 할당 경로와 연결하려는 설계, heap별 상태, 사용량 counter와 event 기록을 같은 경로에 배치하려는 시도, libgcc 재사용이다.

반면 allocator의 자료구조와 모든 할당 경로를 바꾸기 때문에 영향 범위가 넓다. **문제의 도메인은 unwind뿐 아니라 allocator 정확성·동시성·회계까지 포함한다.** 일반적인 backtrace 인프라를 얻기 위해 이 전체 변경을 먼저 가져오는 것은 비용이 크다.

누수 분석 기능이 필요하다면 충분히 발전시킬 가치가 있지만, 현재 보고서의 경로와 delta를 신뢰하기 전에 위 결함을 수정해야 한다.

<a id="comparison"></a>

## 6. 두 PR의 차이를 한눈에 보기

| 비교 항목 | #7522 | #7445 |
| --- | --- | --- |
| 주된 질문 | “어떤 호출 경로에서 장애가 발생했나?” | “이 구간에 남은 메모리는 어떤 경로로 할당됐나?” |
| 주요 실행 시점 | ASSERT/crash, 명시적 backtrace 요청 | malloc/memalign/realloc, capture/report 명령 |
| 테이블 해석기 | TizenRT 내부 자체 EHABI parser | 사용자: libgcc `_Unwind_Backtrace`; 커널: 기존 arch backtrace 경로 |
| 주요 위치 | arch, scheduler, assert, binfmt | libc, MM allocator, heapinfo, mminfo driver |
| kernel 테이블 | 생성 시도하되 여러 RAM 관련 파일 제외 | 새 flag는 사용자 pass에만 추가 |
| flat | 지원 의도 및 커널 엔진 경로 있음. 선택별 검증 필요 | 새 테이블 생성 flag에서 flat 명시적 제외 |
| loadable table | 커널 진단용 등록 배열 새로 추가 | 기존 C++ 예외 table lookup/등록 기반에 의존하는 부분 있음 |
| 현재 thread | 여러 context 경로로 지원 시도 | libc API는 현재 thread만 |
| 다른 task | saved context 사용. 생존·정지 보호 보완 필요 | libc API에서 거절 |
| 다른 CPU의 실행 중 task | IPI 미지원, wrapper에서 0 반환 | 지원하지 않음 |
| ASSERT 연결 | ARMv7-A assert 출력 변경 | 추가하지 않음 |
| allocation history | 없음 | node와 capture table에 기록 |
| 주요 RAM 비용 | frame buffer, parser 작업 상태, 등록 배열 | 모든 관련 heap node 확대 + capture table + 작업 스택 |
| 주요 시간 비용 | 추적을 수행할 때 | 최신 코드는 평상시 allocation에도 수집 시도 |
| 가장 큰 정확성 위험 | 잘못된 시작 상태·범위·ABI 해석·stale table | 잘못된 snapshot 순서·stop 경계·caller 손실·설정별 layout |
| 유지보수 책임 | EHABI subset, ISA context, loader lifetime | allocator invariants, capture session, libgcc/table integration |

### 6.1 단순히 같이 넣으면 안 되는 구체적인 이유

1. **같은 `sched_backtrace()` 이름에 서로 다른 계약이 있다.** #7522는 kernel scheduler 쪽 구현, #7445는 libc 구현이다. 한 링크 단위에 모두 들어가는 구성에서는 중복 심볼 또는 잘못된 구현 선택을 확인해야 한다. protected 구성에서 별도 이미지에 존재하더라도 지원 대상·실패 반환값이 다르다.
2. **`up_backtrace()` 인자 수가 다르다.** #7522는 `asserted_location`을 추가한 5인자 API다. #7445 MM 경로는 4인자 선언·호출을 사용한다. 헤더와 호출자까지 함께 정리해야 한다.
3. **`SCHED_BACKTRACE`를 정의하는 위치와 의미가 다르다.** 하나는 ARM 엔진 선택을 포함하고, 다른 하나는 libc/libgcc 수집 기능을 설명한다. symbol 이름이 같다고 같은 설정이라고 취급할 수 없다.
4. **등록 체계도 다르다.** #7522의 커널 진단용 배열과 기존 사용자 C++/libgcc lookup의 생명주기를 함께 관리해야 한다. 한쪽에 등록했다고 다른 엔진이 자동으로 보게 되지 않는다.
5. **heap과 바이너리 header의 ABI 영향이 겹친다.** #7445의 node/heap layout, #7522의 userspace exidx 게시를 kernel/common/app 전체에서 맞춰야 한다.

근거: [#7522 scheduler][7522-sched], [#7445 libc][7445-libc], [#7522 arch 선언][7522-archh], [#7445 MM 호출][7445-mm-macro]. 실제 merge/cherry-pick은 이번 검토에서 수행하지 않았다.

<a id="cost"></a>

## 7. 비용: 테이블 공간과 할당 기록 공간을 구분해야 한다

### 7.1 #7522의 비용 구조

32비트 ARM에서 최종 exidx 엔트리가 E개라면 **인덱스 자체는 `8 × E`바이트**다. 여기에 extab, 코드 생성 변화, parser 코드 및 정렬 비용이 더해진다. 엔트리 수가 항상 소스 함수 수와 같지는 않다. 병합·제거·CANTUNWIND 및 linker 옵션 영향을 받는다.

현재 ASSERT 주소 버퍼는 `32 × 4 = 128바이트`다. 등록 배열은 32비트 환경에서 항목당 네 개의 32비트 값, 약 `16 × (CONFIG_NUM_APPS + 1)`바이트다. parser의 VRS, 호출 프레임, 로그 함수 stack은 별도다. 이 수치는 소스 자료형의 산술이며 **최종 펌웨어 크기나 최대 stack 사용량의 실측치가 아니다.**[엔진 자료구조][7522-unwind], [ASSERT 버퍼][7522-assert]

예를 들어 E=5,000이면 exidx만 약 39.1KiB다. 테이블이 Flash/XIP에 있으면 그 공간을 매번 heap에서 할당하는 것은 아니다. ELF의 debug 정보 크기 증가와 실제 장비에 적재되는 영역 증가도 구분해야 한다.

### 7.2 #7445의 비용 구조

할당 노드의 비용은 주소 배열 `4 × N`만으로 끝나지 않는다. 기존 단일 caller 필드 제거, 선택적인 sequence, 구조체 정렬, free-node 확대까지 반영해야 한다.[노드 레이아웃][7445-mm-node]

32비트, `mmsize_t=4`, `pid_t=2`, `MM_MIN_CHUNK=16`, HEAPINFO on, FREEINFO off를 가정하면 다음과 같이 계산된다.

| 구성 | alloc node | free node | capture table 엔트리 |
| --- | --- | --- | --- |
| 변경 전 HEAPINFO layout | 16바이트 | 24바이트 | 없음 |
| depth 3, sequence off | 32바이트 | 32바이트 | 32바이트 |
| depth 3, sequence on | 32바이트 | 48바이트 | 32바이트 |
| depth 4, sequence on | 32바이트 | 48바이트 | 36바이트 |
| depth 8, sequence on | 48바이트 | 64바이트 | 52바이트 |

이는 [변경 전 구조체][7445-base-node]와 [현재 구조체][7445-mm-node]의 필드 및 16바이트 정렬로 계산한 값이다. 실제 빌드에서 `sizeof`·offset을 확인해야 한다. FREEINFO, 포인터 크기, 자료형 크기가 바뀌면 다시 계산해야 한다.

기본 1,024개 엔트리라면 capture payload만 depth 3에서 32KiB, depth 4에서 36KiB, depth 8에서 52KiB다. table 자체의 allocator header/정렬과 heap의 고정 상태는 추가다. **대상 heap에서 이 메모리를 확보하므로, 메모리가 부족한 상황을 조사하는 도구가 그 상황 자체를 바꿀 수 있다.**[table 구조 및 용량][7445-mm-table], [할당 위치][7445-capture]

시간 비용은 수집 깊이, 생성된 unwind 규칙, allocator lock 안의 실행량, table 검색 길이와 로그 속도에 따라 달라진다. 양 PR 모두 숫자로 확인된 latency 측정 자료가 이번 검토 근거에는 없으므로 “몇 % 증가한다”는 추정은 제시하지 않는다.

<a id="adoption"></a>

## 8. 권장 도입 순서

### 8.1 우선 목표가 범용 크래시 분석인 경우

**#7522를 기반 후보로 삼되, 기능을 좁혀 수정·검증한 버전을 먼저 도입한다.** 처음부터 모든 ARM 보드와 모든 context를 지원한다고 선언하지 않는다.

1. **지원 범위를 고정한다.** 우선 RTL8730E/AMEBASMART, 사용할 GCC 버전, flat/loadable 중 필요한 구성, Thumb/ARM, SMP 여부를 명시한다.
2. **API와 엔진 선택을 정리한다.** 현재 context, saved fault context, 다른 task의 context를 구분하고, 선택된 구현만 빌드한다. raw fault PC와 반환 주소를 섞지 않는다.
3. **읽기와 parser를 안전하게 만든다.** SP/table 범위, 유효 레지스터, unsupported personality/opcode, 잘린 입력을 확인해 안전하게 중단한다. 손상된 스택에서 진단 코드가 재차 fault를 내지 않아야 한다.
4. **테이블 등록의 수명을 고친다.** 실행 전에 등록하고 unload/reload에 맞춰 교체·해제하며, 분석 중 대상 memory가 사라지지 않도록 한다.
5. **출력에 근거와 제한을 표시한다.** EHABI 성공 프레임과 scan 후보를 구분하고, 제외된 코드·CANTUNWIND·지원하지 않는 opcode 등 종료 이유를 남긴다.
6. **실제 장비에서 검증한다.** 정상 호출, kernel/user ASSERT, Data Abort, IRQ, 앱 반복 재시작, 손상 SP를 구분해 실행한다.

자체 엔진을 유지할 경우 공식 ABI 및 검증된 구현과 대조하는 부담을 수용해야 한다. 정상 user backtrace처럼 libgcc가 잘 맞는 경로는 검증된 runtime을 그대로 사용하는 선택도 가능하다. **공통 API를 만든다는 이유로 모든 경로를 하나의 자체 parser에 강제로 몰 필요는 없다.**

### 8.2 우선 목표가 메모리 누수 후보 조사인 경우

**#7445의 capture 기능을 별도 진단 옵션으로 가져오되, allocator 동작과 보고서 신뢰성을 먼저 수정한다.**

1. 단일 caller 보존 및 header layout 계약을 확정한다. 설정 off가 실제로 기존 동작을 유지하게 한다.
2. backtrace 수집 완료 후 event snapshot을 기록하도록 순서를 고친다.
3. stop과 table/counter/filter snapshot을 원자적으로 확정한다. start 실패는 호출자에게 오류로 전달한다.
4. 대상 PID·구간·문맥에 따라 실제 수집을 제한한다. 커널/IRQ/early boot 정책을 명시한다.
5. 모든 depth에서 bounds-safe한 출력과 실제 frame count를 사용한다.
6. 통제된 allocation 이벤트로 집계식과 caller 경로를 검증하고, 평상시·캡처 중 할당 latency와 RAM 사용량을 측정한다.

단지 사용자 코드에서 `backtrace()`에 해당하는 기능만 필요하면 #7445의 libc wrapper와 테이블 연결을 독립적으로 검토할 수 있다. heap node와 capture 기능 전체를 함께 도입할 필요는 없다.

### 8.3 최종적으로 두 기능이 모두 필요하다면

```text
컴파일·링크·binary table 관리
              ↓
호출 경로 수집 계약
  ├─ 정상 사용자 호출: libgcc 사용 가능
  └─ 저장된 fault/task context: 해당 context를 지원하는 엔진
              ↓
  ├─ ASSERT/crash 보고
  └─ allocation 기록·capture window
```

권장 순서는 **테이블/수집 기반 안정화 → crash 경로 검증 → allocator 기록 기능 추가**다. 두 소비자가 공유해야 할 것은 주소 의미, 유효 길이, 실패 이유, table 수명 계약이다. 최종 로그의 이름이 같다는 이유로 서로 다른 API를 그대로 연결하지 않는다.

<a id="validation"></a>

## 9. 도입 판단을 위한 검증 계획

아래는 **앞으로 실행할 검증 항목**이다. 이번 문서 작성으로 통과했다고 표시한 항목이 아니다.

### 9.1 빌드와 최종 ELF

| 검증 | 확인할 내용 |
| --- | --- |
| 옵션 off 빌드 | 기존 동작·노드 크기·외부 심볼·링크 결과 회귀 여부 |
| 옵션 on 빌드 | EHABI/FP/SP 선택별 소스·선언·중복 정의와 링크 성공 |
| flat 및 loadable | 각 kernel/common/app에 기대한 flags가 실제 전달되는지 |
| C++ 예외 on/off | backtrace용 등록이 예외 설정에 우연히 의존하지 않는지 |
| 최종 table decode | CANTUNWIND 구간, extab, personality, opcode가 parser 지원과 맞는지 |
| XIP/RAM 배치 | PREL31 relocation 범위, 제외 함수, 영역별 text/table 대응 |
| 헤더 ABI | `userspace_s`, `binary_s`, `mm_heap_s`, alloc/free node의 크기와 offset 일치 |

도구 사용 예시다. 파일명은 실제 보드 산출물에 맞춘다.

```sh
arm-none-eabi-readelf -SW firmware.elf
arm-none-eabi-readelf --unwind firmware.elf
arm-none-eabi-nm -n firmware.elf
arm-none-eabi-objdump -d firmware.elf
arm-none-eabi-addr2line -a -f -C -i -e firmware.elf 0xADDRESS
```

`readelf --unwind`와 `addr2line`의 역할은 각각 [GNU readelf][readelf], [GNU addr2line][addr2line] 문서에 설명되어 있다. relocatable app 주소는 맞는 ELF와 load/relocation 정보를 함께 적용해야 하며, XIP 주소에 무조건 임의의 base를 빼면 안 된다.

### 9.2 #7522 기능·안전성

| 시나리오 | 통과 기준 |
| --- | --- |
| 3단계·10단계 정상 함수 호출 | 실제 생성된 함수 프레임과 순서가 맞음 |
| 큰 local 배열·복잡한 frame | multiword extab, 다중 바이트 ULEB128 처리 |
| FP on/off·최적화 변경 | 시작 PC/SP/보존 레지스터가 일관됨 |
| recursion·tail call·inline | 합법적 반복 frame을 버리지 않으며 사라진 frame을 허위 생성하지 않음 |
| user DEBUGASSERT와 Data Abort | 서로 다른 진입 경로를 모두 검증, raw PC 보존 |
| kernel ASSERT·IRQ·nested IRQ | 실제 CPU별 stack 경계와 saved task context 사용 |
| 앱 시작 직후 crash | task 활성화 전에 table 조회 가능 |
| 앱 unload/reload 반복 | stale entry·용량 고갈·해제 메모리 읽기 없음 |
| SMP 다른 CPU에서 실행 중인 task | 미지원이면 명확히 실패. 지원 시 일관된 snapshot 확보 |
| 손상·정렬 안 된 SP, 빈 table, 잘린 extab | fault/hang 없이 중단하고 이유 출력 |
| personality 0/1/2·generic·VFP | 지원 형식은 정확히 해석, 미지원은 header를 opcode로 실행하지 않음 |
| RAM/어셈블리 제외 구간 | 끊긴 이유 표시. scan 후보를 EHABI 프레임으로 표기하지 않음 |

손으로 작성한 assembly의 unwind 설명은 별도다. 컴파일러 C flags만으로 모든 `.S` 프레임이 자동 기술되지는 않는다.[GNU assembler ARM directives][asarm]

### 9.3 #7445 allocator·집계·성능

| 시나리오 | 통과 기준 |
| --- | --- |
| 구간 내 alloc 후 free | 새로 남은 할당과 delta 모두 0 |
| 이전 alloc의 free | FREED와 감소 counter가 일치 |
| 이전/신규 블록의 in-place·이동 realloc | 반복 realloc까지 분류·old/new size·owner가 일관됨 |
| 같은 주소 재사용 | 이전 history와 새 allocation을 혼동하지 않음 |
| 새 malloc의 경로 확인 | node와 capture table이 같은 새 backtrace를 보유 |
| start/stop 사이 경쟁 | 종료 순간의 table·counter·PID가 하나의 snapshot |
| PID 지정·생략·변경 | session 범위 유지 또는 명확한 오류 |
| 1,024개 초과·table malloc 실패 | 유실/실패 표시 및 일치 보장 중단을 명확히 알림 |
| HEAPINFO on/off, depth −1/0/1/2/3/8 | 의도한 layout, bounds-safe 출력, 기존 caller 보존 |
| 여러 heap·여러 CPU | seqno와 capture session의 동기화 계약 충족 |
| 앱 종료·heap unload·PID 재사용 | stale heap/TCB 접근 없이 종료 |
| 진단 off/평상시/캡처 중 | malloc/free/realloc 최악 지연, lock 시간, Flash/RAM·stack 비용 측정 |

### 9.4 증거를 남기는 단위

최소한 `PR head 또는 적용 commit`, `.config`, toolchain 버전, ELF/map, 바이너리별 로드 주소, 테스트 입력, 실제 로그를 함께 보관해야 한다. **소스 리뷰, host decoder 테스트, CI 빌드, emulator 실행, 실제 보드 실행은 각각 별도의 증거**로 기록한다. 일부 테스트 통과를 다른 범위의 완료로 바꾸어 표현하지 않는다.

## 10. 최종 권고

**언와인드 테이블 기반 크래시 분석의 도입 후보는 #7522다.** 커널 ASSERT, 저장된 context, loadable binary를 다룬다는 점이 요구에 맞는다. 다만 현재 구현의 정확성·안전성 문제를 수정한 후, 실제 사용하는 보드와 빌드 구성에서 제한된 범위부터 검증해야 한다.

**#7445는 그 기반과 별도로 평가할 메모리 진단 기능이다.** 사용자 정상 호출에서 libgcc를 재사용하는 접근은 합리적이지만, 현재 allocator 기록·보고 경로에는 신뢰성을 해치는 문제가 있다. 누수 분석이 목적일 때 수정 후 도입하고, 평상시 비용을 측정해 활성화 범위를 정하는 것이 좋다.

현재 선택은 “둘 중 하나를 그대로 머지”가 아니라 **#7522를 크래시 분석 기반으로 다듬고, 필요할 때 #7445의 할당 분석을 정리해 얹는 것**을 권장한다.

## 부록 A. 구현을 다시 읽을 때의 파일 안내

전체 변경 파일은 [#7522 Files changed][files7522], [#7445 Files changed][files7445]에서 확인할 수 있다. 아래는 기능별로 읽을 순서다.

| PR | 영역 | 먼저 읽을 파일과 역할 |
| --- | --- | --- |
| #7522 | 설정/선택 | `os/arch/arm/Kconfig`, `amebasmart/Make.defs`, `armv7-a/Toolchain.defs`, RTL8730E `Make.defs` |
| #7522 | 엔진 | `common/arm_backtrace_unwind.c`, `.h`; 스캔은 `arm_backtrace_sp.c` |
| #7522 | API | `os/arch/arm/include/arch.h`, `os/kernel/sched/sched_backtrace.c` |
| #7522 | 장애 진입 | `armv7-a/arm_assert.c`, `arm_syscall.c` |
| #7522 | 테이블 전달 | `userspace.h`, `binfmt.h`, `elf.h`, `up_userspace.c`, `libelf_load.c`, `xipelf.c` |
| #7522 | 로드 수명 | `binfmt_execmodule.c`, `binfmt_loadbinary.c`와 변경되지 않은 unload 경로 함께 확인 |
| #7522 | 테이블 제외 | boot/MMU/cpupause/dataabort, IRQ/semaphore spinlock, FTL/crypto/flash API 및 Flash RAM 파일 |
| #7522 | 예제 | `apps/examples/backtrace_test/`의 Kconfig·Makefile·main |
| #7445 | 사용 명령/driver | `utils_heapinfo.c`, `mminfo.c`, `ioctl.h` |
| #7445 | libgcc wrapper | `lib/libc/sched/sched_backtrace.c`, libc `Make.defs`, `os/include/sched.h` |
| #7445 | 설정/엔진 연결 | kernel/arch/MM Kconfig, AMEBASMART Make.defs, ARMv7-A Toolchain.defs, RTL8730E Make.defs/defconfig |
| #7445 | allocator 계약 | `mm.h`, `mm_backtrace.c`, `mm_initialize.c`, `mm_heap/Make.defs` |
| #7445 | 기록·집계 | `mm_malloc.c`, `mm_free.c`, `mm_memalign.c`, `mm_realloc.c`, `mm_heapinfo_utils.c` |
| #7445 | 보고서 | `mm_heapinfo_parse_heap.c`, `mm_heapinfo_backtrace.c`, `mm_heap_dbg.c`, `mem_leak_checker.c` |

#7445에는 ARM Thumb backtrace의 코드 범위를 Flash text 심볼로 바꾸는 수정과 memalign의 포인터 연산을 byte 단위로 바꾸는 변경도 함께 있다. 따라서 “unwind table 생성 flag만 추가한 PR”로 리뷰 범위를 좁히면 안 된다.[#7445 전체 변경][files7445]

## 부록 B. 근거와 재검토 방법

문서 내 소스 링크는 각 PR의 검토 head SHA에 고정했다. 따라서 PR이 갱신되어도 여기서 지적한 줄과 당시 구현을 다시 확인할 수 있다. 공식 명세·컴파일러 문서는 원문을 링크했으며, 실제 도입 때에는 사용할 toolchain 버전과 생성 ELF를 기준으로 재검증한다.

공개 리뷰에서 확인된 #7445의 요청은 [공백 정리][7445-review1], [utils 커밋 분리][7445-review2], [수정 답변][7445-review3]다. 두 PR 모두 CI 성공 사실과 별개로 이 문서의 알고리즘·집계 검토를 승인받았다는 의미는 아니다.

이번 작업의 산출물은 이 검토 문서다. PR 적용, 소스 수정, 펌웨어 빌드, 장비 fault 유발, commit/push 및 GitHub 댓글 작성은 수행하지 않았다.

<!-- Source links: PR source snapshots are pinned to the reviewed SHA. -->

[pr7522]: https://github.com/Samsung/TizenRT/pull/7522
[pr7445]: https://github.com/Samsung/TizenRT/pull/7445
[api7522]: https://api.github.com/repos/Samsung/TizenRT/pulls/7522
[api7445]: https://api.github.com/repos/Samsung/TizenRT/pulls/7445
[ci7522]: https://github.com/Samsung/TizenRT/pull/7522/checks
[ci7445]: https://github.com/Samsung/TizenRT/pull/7445/checks
[files7522]: https://github.com/Samsung/TizenRT/pull/7522/files
[files7445]: https://github.com/Samsung/TizenRT/pull/7445/files
[ehabi]: https://github.com/ARM-software/abi-aa/blob/main/ehabi32/ehabi32.rst
[aaelf]: https://github.com/ARM-software/abi-aa/blob/main/aaelf32/aaelf32.rst
[gcccodegen]: https://gcc.gnu.org/onlinedocs/gcc/Code-Gen-Options.html
[gccopt]: https://gcc.gnu.org/onlinedocs/gcc/Optimize-Options.html
[cm33]: https://developer.arm.com/documentation/100235/0004/the-cortex-m33-processor/exception-model/exception-entry-and-return
[cm33regs]: https://developer.arm.com/documentation/100230/0004/functional-description/programmers-model/processor-core-registers-summary
[ldarm]: https://sourceware.org/binutils/docs/ld/ARM.html
[readelf]: https://sourceware.org/binutils/docs/binutils/readelf.html
[addr2line]: https://sourceware.org/binutils/docs/binutils/addr2line.html
[asarm]: https://sourceware.org/binutils/docs/as/ARM-Directives.html
[crashguide]: ../HowToDebugACrash.md
[7445-review1]: https://github.com/Samsung/TizenRT/pull/7445#discussion_r3733069955
[7445-review2]: https://github.com/Samsung/TizenRT/pull/7445#discussion_r3733074377
[7445-review3]: https://github.com/Samsung/TizenRT/pull/7445#discussion_r3750906037
[7522-linker]: https://github.com/Samsung/TizenRT/blob/cfdc05ec99e7a97a44dd3c93066d0d64ffe115dc/build/configs/rtl8730e/scripts/rlx8730e_img2.ld#L166-L201
[7522-userlinker]: https://github.com/Samsung/TizenRT/blob/cfdc05ec99e7a97a44dd3c93066d0d64ffe115dc/build/configs/rtl8730e/scripts/xipelf/userspace_all.ld#L22-L61
[7522-oldfp]: https://github.com/Samsung/TizenRT/blob/cfdc05ec99e7a97a44dd3c93066d0d64ffe115dc/os/arch/arm/src/armv7-a/arm_backtrace_fp.c#L122-L161
[7522-assert]: https://github.com/Samsung/TizenRT/blob/cfdc05ec99e7a97a44dd3c93066d0d64ffe115dc/os/arch/arm/src/armv7-a/arm_assert.c#L620-L649
[7522-sched]: https://github.com/Samsung/TizenRT/blob/cfdc05ec99e7a97a44dd3c93066d0d64ffe115dc/os/kernel/sched/sched_backtrace.c#L108-L163
[7522-kconfig]: https://github.com/Samsung/TizenRT/blob/cfdc05ec99e7a97a44dd3c93066d0d64ffe115dc/os/arch/arm/Kconfig#L507-L559
[7522-boardflags]: https://github.com/Samsung/TizenRT/blob/cfdc05ec99e7a97a44dd3c93066d0d64ffe115dc/build/configs/rtl8730e/Make.defs#L157-L170
[7522-toolchain]: https://github.com/Samsung/TizenRT/blob/cfdc05ec99e7a97a44dd3c93066d0d64ffe115dc/os/arch/arm/src/armv7-a/Toolchain.defs#L141-L178
[7522-unwind]: https://github.com/Samsung/TizenRT/blob/cfdc05ec99e7a97a44dd3c93066d0d64ffe115dc/os/arch/arm/src/common/arm_backtrace_unwind.c
[7522-register]: https://github.com/Samsung/TizenRT/blob/cfdc05ec99e7a97a44dd3c93066d0d64ffe115dc/os/arch/arm/src/common/arm_backtrace_unwind.c#L153-L212
[7522-context]: https://github.com/Samsung/TizenRT/blob/cfdc05ec99e7a97a44dd3c93066d0d64ffe115dc/os/arch/arm/src/common/arm_backtrace_unwind.c#L686-L856
[7522-frame]: https://github.com/Samsung/TizenRT/blob/cfdc05ec99e7a97a44dd3c93066d0d64ffe115dc/os/arch/arm/src/common/arm_backtrace_unwind.c#L480-L565
[7522-opcodes]: https://github.com/Samsung/TizenRT/blob/cfdc05ec99e7a97a44dd3c93066d0d64ffe115dc/os/arch/arm/src/common/arm_backtrace_unwind.c#L293-L473
[7522-assertscan]: https://github.com/Samsung/TizenRT/blob/cfdc05ec99e7a97a44dd3c93066d0d64ffe115dc/os/arch/arm/src/common/arm_backtrace_unwind.c#L742-L787
[7522-irq]: https://github.com/Samsung/TizenRT/blob/cfdc05ec99e7a97a44dd3c93066d0d64ffe115dc/os/arch/arm/src/amebasmart/amebasmart_irq.c#L51-L72
[7522-make]: https://github.com/Samsung/TizenRT/blob/cfdc05ec99e7a97a44dd3c93066d0d64ffe115dc/os/arch/arm/src/amebasmart/Make.defs#L66-L134
[7522-sp]: https://github.com/Samsung/TizenRT/blob/cfdc05ec99e7a97a44dd3c93066d0d64ffe115dc/os/arch/arm/src/common/arm_backtrace_sp.c#L253-L319
[7522-archh]: https://github.com/Samsung/TizenRT/blob/cfdc05ec99e7a97a44dd3c93066d0d64ffe115dc/os/arch/arm/include/arch.h#L236-L289
[7522-exec]: https://github.com/Samsung/TizenRT/blob/cfdc05ec99e7a97a44dd3c93066d0d64ffe115dc/os/binfmt/binfmt_execmodule.c#L345-L368
[7522-loadbinary]: https://github.com/Samsung/TizenRT/blob/cfdc05ec99e7a97a44dd3c93066d0d64ffe115dc/os/binfmt/binfmt_loadbinary.c#L211-L224
[7522-xip]: https://github.com/Samsung/TizenRT/blob/cfdc05ec99e7a97a44dd3c93066d0d64ffe115dc/os/binfmt/libxipelf/xipelf.c#L130-L140
[7522-elfload]: https://github.com/Samsung/TizenRT/blob/cfdc05ec99e7a97a44dd3c93066d0d64ffe115dc/os/binfmt/libelf/libelf_load.c#L329-L347
[7522-tests]: https://github.com/Samsung/TizenRT/blob/cfdc05ec99e7a97a44dd3c93066d0d64ffe115dc/apps/examples/backtrace_test/backtrace_main.c#L48-L220
[7445-cli]: https://github.com/Samsung/TizenRT/blob/c6b7d2c1ff14f6744dec3ec4aab49c021b0bbb4f/apps/system/utils/utils_heapinfo.c#L215-L397
[7445-libc]: https://github.com/Samsung/TizenRT/blob/c6b7d2c1ff14f6744dec3ec4aab49c021b0bbb4f/lib/libc/sched/sched_backtrace.c#L52-L177
[7445-exidx]: https://github.com/Samsung/TizenRT/blob/c6b7d2c1ff14f6744dec3ec4aab49c021b0bbb4f/apps/platform/gnu/gnu_unwind_find_exidx.c#L59-L108
[7445-userspace]: https://github.com/Samsung/TizenRT/blob/c6b7d2c1ff14f6744dec3ec4aab49c021b0bbb4f/os/userspace/up_userspace.c#L108-L114
[7445-exec]: https://github.com/Samsung/TizenRT/blob/c6b7d2c1ff14f6744dec3ec4aab49c021b0bbb4f/os/binfmt/binfmt_execmodule.c#L139-L143
[7445-boardflags]: https://github.com/Samsung/TizenRT/blob/c6b7d2c1ff14f6744dec3ec4aab49c021b0bbb4f/build/configs/rtl8730e/Make.defs#L178-L198
[7445-toolchain]: https://github.com/Samsung/TizenRT/blob/c6b7d2c1ff14f6744dec3ec4aab49c021b0bbb4f/os/arch/arm/src/armv7-a/Toolchain.defs#L141-L149
[7445-mm-macro]: https://github.com/Samsung/TizenRT/blob/c6b7d2c1ff14f6744dec3ec4aab49c021b0bbb4f/os/include/tinyara/mm/mm.h#L384-L495
[7445-mm-node]: https://github.com/Samsung/TizenRT/blob/c6b7d2c1ff14f6744dec3ec4aab49c021b0bbb4f/os/include/tinyara/mm/mm.h#L297-L379
[7445-mm-force]: https://github.com/Samsung/TizenRT/blob/c6b7d2c1ff14f6744dec3ec4aab49c021b0bbb4f/os/include/tinyara/mm/mm.h#L237-L269
[7445-mm-table]: https://github.com/Samsung/TizenRT/blob/c6b7d2c1ff14f6744dec3ec4aab49c021b0bbb4f/os/include/tinyara/mm/mm.h#L521-L605
[7445-mm-kconfig]: https://github.com/Samsung/TizenRT/blob/c6b7d2c1ff14f6744dec3ec4aab49c021b0bbb4f/os/mm/Kconfig#L160-L199
[7445-malloc]: https://github.com/Samsung/TizenRT/blob/c6b7d2c1ff14f6744dec3ec4aab49c021b0bbb4f/os/mm/mm_heap/mm_malloc.c#L291-L303
[7445-capture]: https://github.com/Samsung/TizenRT/blob/c6b7d2c1ff14f6744dec3ec4aab49c021b0bbb4f/os/mm/mm_heap/mm_heapinfo_utils.c#L72-L342
[7445-parse]: https://github.com/Samsung/TizenRT/blob/c6b7d2c1ff14f6744dec3ec4aab49c021b0bbb4f/os/mm/mm_heap/mm_heapinfo_parse_heap.c#L190-L202
[7445-report]: https://github.com/Samsung/TizenRT/blob/c6b7d2c1ff14f6744dec3ec4aab49c021b0bbb4f/os/mm/mm_heap/mm_heapinfo_parse_heap.c#L369-L455
[7445-defconfig]: https://github.com/Samsung/TizenRT/blob/c6b7d2c1ff14f6744dec3ec4aab49c021b0bbb4f/build/configs/rtl8730e/loadable_ext_ddr_st7785/defconfig#L417-L422
[7445-defconfig-fp]: https://github.com/Samsung/TizenRT/blob/c6b7d2c1ff14f6744dec3ec4aab49c021b0bbb4f/build/configs/rtl8730e/loadable_ext_ddr_st7785/defconfig#L1412-L1415
[7445-driver]: https://github.com/Samsung/TizenRT/blob/c6b7d2c1ff14f6744dec3ec4aab49c021b0bbb4f/os/drivers/memory/mminfo.c#L106-L226
[7445-stub]: https://github.com/Samsung/TizenRT/blob/c6b7d2c1ff14f6744dec3ec4aab49c021b0bbb4f/os/mm/mm_heap/mm_backtrace.c#L42-L76
[7445-dump]: https://github.com/Samsung/TizenRT/blob/c6b7d2c1ff14f6744dec3ec4aab49c021b0bbb4f/os/mm/mm_heap/mm_heapinfo_backtrace.c#L80-L205
[7445-memleak]: https://github.com/Samsung/TizenRT/blob/c6b7d2c1ff14f6744dec3ec4aab49c021b0bbb4f/os/kernel/debug/mem_leak_checker.c#L360-L399
[7445-make]: https://github.com/Samsung/TizenRT/blob/c6b7d2c1ff14f6744dec3ec4aab49c021b0bbb4f/os/arch/arm/src/amebasmart/Make.defs#L109-L125
[7445-stackexclude]: https://github.com/Samsung/TizenRT/blob/c6b7d2c1ff14f6744dec3ec4aab49c021b0bbb4f/os/mm/mm_heap/mm_heapinfo_utils.c#L432-L453
[7445-sem]: https://github.com/Samsung/TizenRT/blob/c6b7d2c1ff14f6744dec3ec4aab49c021b0bbb4f/os/mm/mm_heap/mm_sem.c#L144-L179
[7445-base-node]: https://github.com/Samsung/TizenRT/blob/7f257954e75e4e01323102244d2c6e551e073e21/os/include/tinyara/mm/mm.h#L257-L297
