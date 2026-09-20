# TizenRT PR #7536 — Binary Manager 복구 사유 분리 검토

작성일: 2026-09-19\
대상: [Samsung/TizenRT PR #7536][pr]\
검토 head: `c7a98ec37fb0240a67cd99e8b09ca2fc775db33d`\
산출물: 코드 리뷰, 수정 패치, 재실행 가능한 호스트 회귀 검증

## 1. 결론

**복구 사유를 세분화하고 각 실패 경로에서 기록하는 방향은 적절하다. 다만 영속 reason 값의 호환성을 수정하고, “기존 사유 보존”이라는 PR 설명을 명확하게 한 뒤 도입하는 것을 권한다.**

검토 결과는 두 축으로 구분한다.

| 축 | 발견 사항 | 심각도·성격 | 제공한 조치 |
| --- | --- | --- | --- |
| Standards | 내부 전용 setter를 public header에 선언 | P3, 명시된 규약 M09 위반 | internal header로 이동 |
| Standards | COMMON/USER 선택 분기가 세 번 반복 | P3, 설계 개선 권고 | 지역 변수로 한 번 계산 |
| Spec | BP에 저장되는 숫자 `16`의 의미 재사용 | P2, 진단 데이터 호환성 | UNKNOWN=16 유지, COMMON=17 추가 |
| Spec | 기존 사유 보존과 새 사유 기록 요구가 충돌 | 명세 모순·정책 확인 사항 | 위험한 의미 변경 없이 문서에 선택지와 근거 정리 |

여기서 P2는 부팅 불능을 확인했다는 뜻이 아니다. 기존 또는 외부 BP가 과거 정의의 `UNKNOWN=16`을 담고 있다면 새 정의로 COMMON 복구로 해석되는 문제다. **실제 배포 장비에 그런 BP가 존재한다는 증거는 확인하지 않았다.**

수정 코드는 [적용 가능한 패치](pr7536/0001-preserve-bp-reason-values-and-internal-api.patch)로 제공한다. PR head에 적용한 복사본을 검증했으며, 현재 `codex/docs`의 TizenRT 제품 소스에는 적용하지 않았다.

## 2. 검토 기준과 확인한 증거

| 항목 | 확인 결과 |
| --- | --- |
| PR 제목 | `os/kernel/binary_manager: Separate binary_manager recover reason` |
| 확인 시 상태 | Open, draft 아님, 미병합 |
| PR head | `c7a98ec37fb0240a67cd99e8b09ca2fc775db33d` |
| 조회 시 base branch SHA | `94a61e7e2998ef4c4f77331d13b31986ec97b726` |
| 실제 merge-base | `df09bb8a19eed8e63b66a5b91ba349aefd133b20` |
| 변경 범위 | 1개 커밋, 5개 파일, +53 / −9줄 |
| 공개 리뷰·댓글 | 조회한 reviews, review comments, issue comments 모두 없음 |
| head CI | check run 1개 success, commit status 20개 success |
| 자체 검증 | 원본·수정본 7개 매크로 구성씩 호스트 빌드, 총 28회 실행 |
| 미수행 | 전체 TizenRT 타깃 빌드, 에뮬레이터·보드 부팅, 실제 Flash BP 읽기/쓰기 및 전원 차단 실험 |

PR 메타데이터 및 CI 근거: [PR API][prapi], [checks][checks], [commit status API][status]. CI 성공과 아래 자체 검증은 서로 다른 증거다.

변경 기준을 다음 명령으로 고정했다. 단순히 현재 `codex/docs`와 PR head를 비교하지 않았다.

```sh
git diff 94a61e7e2998ef4c4f77331d13b31986ec97b726...c7a98ec37fb0240a67cd99e8b09ca2fc775db33d
git log --oneline 94a61e7e2998ef4c4f77331d13b31986ec97b726..c7a98ec37fb0240a67cd99e8b09ca2fc775db33d
```

Standards와 Spec은 `code-review` 스킬에 따라 별도 검토자가 독립적으로 확인했다. 요구사항은 제공된 PR 본문과 커밋 메시지를 사용했다. 별도 originating issue 참조는 없었다. 이 저장소에는 해당 스킬이 가정한 `docs/agents/issue-tracker.md`가 없어 GitHub API로 직접 조회했다. 향후 스킬의 자동 issue 연동을 구성하려면 `/setup-matt-pocock-skills`를 사용할 수 있으며, 이번 리뷰의 진행에는 필요하지 않았다.

## 3. BP와 복구 사유를 이해하기

### 3.1 BP는 다음 부팅에서 사용할 바이너리를 알려주는 기록이다

Boot Parameter, 이하 BP는 커널·앱·리소스의 A/B 파티션 중 어느 쪽을 사용할지 알려준다. 이 코드에서는 BP 자체도 두 슬롯에 저장하며, 각 슬롯은 4KiB다. BP version은 어느 기록이 최신인지 판단하는 값이고, format version은 데이터 배치 형식을 나타낸다.[상수·구조체][types], [생성 도구의 형식 설명][mkbp]

```text
바이너리의 A/B 세트
  세트 A: kernel A + common/app A + resource A
  세트 B: kernel B + common/app B + resource B

BP의 두 저장 슬롯
  BP slot 0: version, 선택 세트, 주소/앱 목록, 마지막 갱신 사유
  BP slot 1: version, 선택 세트, 주소/앱 목록, 마지막 갱신 사유
```

바이너리 세트 A/B와 BP slot 0/1은 서로 다른 개념이다. “A로 복구한다”는 말과 “BP slot 0에 쓴다”는 말은 동일하지 않다.

### 3.2 서로 비슷한 세 종류의 값

| 값 | 저장 위치 | 의미 |
| --- | --- | --- |
| `g_bp_recovery_info.recovery_reason` | 실행 중 RAM | 다음 BP 복구에 기록할 원인 |
| `bp_data->tail.bp_update_reason` | BP의 마지막 1바이트 | 마지막 BP 갱신 원인. Flash에 기록됨 |
| `REBOOT_SYSTEM_BINARY_UPDATE` 등 | 별도의 reboot reason 경로 | 왜 시스템을 재부팅하는지 |

이번 PR은 주로 첫 번째 값에서 두 번째 값으로 원인을 전달한다. 재부팅 원인 체계를 새로 세분화하는 PR은 아니다.[복구 상태와 tail][types], [복구 함수][recover]

BP tail의 reason은 **현재 1바이트 값 하나**다. 전체 복구 이력이나 “이전 사유 + 현재 사유” 목록을 저장하지 않는다. 기존 `mkbootparam.py`도 이 필드를 마지막 성공한 BP 갱신 사유로 설명한다.[형식 설명][mkbp]

## 4. PR의 실제 변경 내용

### 4.1 변경 파일별 역할

| 파일 | 변경 |
| --- | --- |
| `os/include/tinyara/binary_manager.h` | reason setter 선언 추가 |
| `binary_manager_internal.h` | reason enum 세분화, recovery 상태에 `uint8_t recovery_reason` 추가 |
| `binary_manager_bootparam.c` | 검사 실패별 사유 선택, setter 구현, 재구성 함수에 reason 전달, 유효 BP의 논리 구조체 복사 |
| `binary_manager_load.c` | 서명·헤더·로드 실패에 COMMON/USER 사유 설정 |
| `binary_manager_resource.c` | 리소스 검증·마운트 실패에 RESOURCE 사유 설정 |

전체 변경: [Files changed][files]. 기존 A/B 세트 검증·버전 비교·BP 슬롯 선택·CRC 계산 알고리즘을 새로 설계하는 변경은 아니다.

### 4.2 원인 코드의 변화

| 숫자 | 변경 전 | PR head | 제공한 수정안 |
| --- | --- | --- | --- |
| 0 | INITIALIZED | 동일 | 동일 |
| 1–7 | BOOTLOADER 원인 영역 | 동일 | 동일 |
| 8 | BINARY_MANAGER_SWAP | 동일 | 동일 |
| 9 | BINARY_MANAGER_UPDATE | 동일 | 동일 |
| 10 | RECOVERY_USER | 동일 | 동일 |
| 11 | RECOVERY_RESOURCE | 동일 | 동일 |
| 12 | SET_ALIGNMENT | RECOVERY_SET_ALIGNMENT | PR과 동일 |
| 13 | SPECIFIC_1 | RECOVERY_NO_VALID_BP | PR과 동일 |
| 14 | SPECIFIC_2 | RECOVERY_KERNEL_ADDR_MISMATCH | PR과 동일 |
| 15 | SPECIFIC_3 | RECOVERY_BOOTLOADER_RECOVERED | PR과 동일 |
| 16 | UNKNOWN | RECOVERY_COMMON | **UNKNOWN 유지** |
| 17 | 정의 없음 | UNKNOWN | **RECOVERY_COMMON 추가** |

근거: [변경 전 enum][baseenum], [PR enum][enum].

13–15는 기존 generic 슬롯에 이름을 붙이는 변경이지만, 사내/외부 구현이 SPECIFIC 값을 이미 사용했다면 의미 충돌 여부를 별도로 확인해야 한다. 공개 소스만으로 외부 사용 여부를 판단하지 않는다. 16은 이미 UNKNOWN이라는 의미가 있었기 때문에 같은 취급을 하지 않았다.

### 4.3 부팅 시 검사 경로

`binary_manager_check_bootparam_set()`는 recovery 상태를 초기화하고 BP를 읽는다. 다음 순서로 원인을 고른다.[검사 함수][check]

| 검사 결과 | 새 사유 |
| --- | --- |
| BP 갱신/검색 실패 또는 BP pointer 없음 | NO_VALID_BP |
| kernel/common/app/resource의 선택 세트 불일치 | SET_ALIGNMENT |
| BP의 커널 주소와 등록된 파티션 주소 불일치 | KERNEL_ADDR_MISMATCH |
| 기존 BP reason이 bootloader 영역 1–7 | BOOTLOADER_RECOVERED |
| 위 조건 없음 | 검사 성공. 복구하지 않음 |

여러 문제가 동시에 있으면 먼저 검사한 조건이 기록된다. 예를 들어 세트 불일치와 커널 주소 불일치가 동시에 있으면 SET_ALIGNMENT가 우선한다. 이 PR은 복수 원인 bitmask를 추가하지 않는다.

`NO_VALID_BP`는 “Flash에 CRC-invalid BP만 존재함”을 정확히 진단한 값으로 좁혀 읽으면 안 된다. 기존 `binary_manager_update_bpinfo()`의 실패에는 read/open/allocation 실패도 포함될 수 있다. 현재 분류는 복구를 시작한 조건을 기록하는 수준이다.

RTL8730E board boot 코드는 check 실패 직후 recover를 호출하므로 여기서는 별도 setter 호출이 필요 없다.[보드 호출][board]

### 4.4 직접 복구를 요청하는 경로

| 실제 호출 지점 | 복구 전 설정하는 reason |
| --- | --- |
| board boot의 check 실패 | check 내부에서 선택한 원인 |
| common/app 서명 실패 | COMMON 또는 USER |
| common/app 헤더·CRC 검사 실패 | COMMON 또는 USER |
| common/app 로드 실패 | COMMON 또는 USER |
| resource 서명 실패 | RESOURCE |
| resource 헤더 검증 또는 mount 실패 후 복구 | RESOURCE |

따라서 실제 recover 호출 6곳은 원인 설정이 연결되어 있다. COMMON 옵션이 꺼진 빌드는 USER를 사용한다.[load 경로][load], [resource 경로][resource]

### 4.5 재구성·기록 경로

```text
check 또는 직접 setter로 원인 설정
  → recover_bootparam_set()
  → A/B 세트를 검증하고 버전 기준으로 대상 선택
  → make_bootparam_from_partitions(..., recovery_reason)
      → valid BP가 있으면 논리 구조체 복사, 없으면 0 초기화
      → 새 version / format / active set / 주소 / 앱 / resource 설정
      → tail.bp_update_reason = 이번 원인
  → write_bootparam_to_slot()
      → 4KiB buffer를 0xff로 초기화
      → head와 마지막 tail 배치
      → CRC 계산 후 기록
  → 재부팅 요청
```

근거: [재구성 함수][make], [복구 함수][recover], [직렬화 함수][write].

여기서 복사하는 `binmgr_bpdata_t`는 head+tail의 **논리 구조체**다. Flash에 있는 4KiB 전체를 복사하는 것이 아니다. 가운데 reserved 영역은 writer가 다시 0xff로 채우므로 “기존 BP의 모든 byte를 보존한다”는 설명도 맞지 않는다.

## Standards

독립 Standards 검토 결과다. 기능·명세 결과와 합쳐 우선순위를 다시 매기지 않는다.

### S1. [P3] 내부 setter의 public header 노출

- 위치: [`os/include/tinyara/binary_manager.h:314`][public].
- 규약: [CodingStyleGuide M09][m09]는 모듈 내부에서 호출하는 함수를 internal header에 선언하도록 규정한다.
- 실제 caller는 `binary_manager_load.c`, `binary_manager_resource.c`이며 둘 다 internal header를 include한다. reason enum도 내부 헤더에 있다.
- 수정: setter 선언만 `binary_manager_internal.h`의 `CONFIG_USE_BP` 조건 아래로 이동했다.

기존 check/recover API는 board 코드에서도 호출되므로 public 선언을 유지했다. 이 finding은 현재 빌드 실패나 메모리 오류를 뜻하지 않는다.

### S2. [P3, 휴리스틱] 동일한 reason 선택 분기 중복

- 위치: [`binary_manager_load.c:206–210, 250–254, 311–315`][load].
- 분류: Duplicated Code에 해당할 수 있는 유지보수 개선 권고. 명시적 규약 위반이나 현재 기능 오류와 구분한다.
- 수정: `CONFIG_USE_BP` 아래의 지역 변수로 reason을 한 번 계산하고 세 실패 경로가 사용한다. `bin_idx`는 함수 안에서 바뀌지 않으므로 선택 결과가 유지된다.

새 helper 계층이나 외부 API를 만들 필요 없이 중복만 줄였다.

Standards 요약: 의무 규칙 위반 1건, 설계 개선 권고 1건. 이 축의 최고 심각도는 P3이며 두 항목 모두 제공 패치에 반영했다.

## Spec

독립 Spec 검토 결과다. 요구사항은 PR 본문을 기준으로 한다.

### F1. [P2] format-v2의 영속 reason `16`을 다른 의미로 재사용

요구사항은 새 Binary Manager 복구 원인을 추가하는 것이다. 그런데 enum의 값은 메모리 내부 상수로만 끝나지 않고 BP 마지막 byte에 저장된다. format version은 2로 유지된다.[enum][enum], [tail][types], [writer][write]

```text
기존 정의로 작성한 BP
  format = 2, reason = 16 → UNKNOWN

PR head의 정의로 해석
  format = 2, reason = 16 → COMMON 복구
```

동일한 데이터에 다른 의미를 붙이게 되어 사유 구분이라는 기능의 정확성을 해친다. 반대로 새 COMMON=16 기록을 구버전 정의로 해석하면 UNKNOWN이 된다. 이 문제는 코드 정의·직렬화 계약으로 확인할 수 있다.

다만 다음 제한을 분명히 한다.

- 공개 소스의 `BP_UPDATE_UNKNOWN` 검색 결과는 enum 선언 한 곳이었다. 기존 코드에서 16을 기록하는 producer는 확인하지 못했다.
- `mkbootparam.py`가 처음 만드는 reason은 0이다.
- 부트로더 release note는 0–4를 열거한다. 이를 근거로 부트로더가 16을 실제 기록한다거나 부팅에 실패한다고 주장하지 않는다.[생성 도구][mkbp], [부트로더 이력][bootnote]
- 회귀 검증은 **기존 계약의 값 16을 담은 모델 입력**을 만들어 충돌을 확인했다. 실제 장비에서 채취한 BP 이미지가 아니다.

최소 수정은 `UNKNOWN=16`을 유지하고 새 COMMON에 17을 부여하는 것이다. 공개 소스에 UNKNOWN을 배열 크기·최댓값 sentinel로 사용하는 지점은 없음을 확인했다. 외부 decoder도 새 COMMON 값에 맞게 갱신해야 한다.

### F2. 명세 모순: 기존 reason은 실제로 보존되지 않는다

PR 본문에는 `Preserve valid bp's recovery_reason`과 `This reason is written into BP`라는 요구가 함께 있다. 전자는 기존 원인 보존, 후자는 앞에서 선택한 새 원인 기록으로 읽힌다.

실제 코드는 아래 순서다.[make 함수][make]

```c
/* 기존 BP의 tail도 함께 복사한다. */
memcpy(bp_data, &g_bp_info.bp_data[g_bp_recovery_info.inuse_idx],
       sizeof(binmgr_bpdata_t));

/* 뒤에서 기존 reason을 새 reason으로 덮는다. */
bp_data->tail.bp_update_reason = reason;
```

따라서 **복사 후에도 기존 원인 이력이 남는다는 주장은 코드와 맞지 않는다.** 현재 tail에는 reason 한 필드뿐이다. 한 필드에 두 독립 값을 모두 보존하는 기능도 없다.

하지만 무조건 기존 reason을 유지하도록 고치면 안 된다. 예를 들어 원래 값이 BOOTLOADER 영역 1–7이면:

```text
bootloader reason 1
  → Binary Manager가 다시 검증·복구
  → 기존 reason 1을 그대로 유지하는 식으로 수정
  → 다음 부팅에서도 check가 reason 1을 보고 다시 복구 요청
```

실제 check/rebuild 함수를 사용하는 호스트 테스트에서 “새 15를 기록하면 해당 조건은 해소되고, 예전 1–7을 다시 넣으면 check가 실패한다”는 판단을 확인했다. 전체 reboot loop를 보드에서 실행했다는 뜻은 아니다.[bootloader reason 검사][check]

**현재 필드 정의와 가장 일관된 정책은 최신 갱신 사유만 저장하는 것이다.** 이 경우 PR 설명을 “새 원인을 기록한다. 이전 원인은 보존하지 않는다”로 명확히 해야 한다. 이전 bootloader의 세부 원인까지 영속적으로 남겨야 한다면 별도 이력 필드/저장소와 버전·구버전 호환 설계가 필요하다.

제공 패치는 기존의 새 원인 기록 정책을 유지한다. 이 모순을 해결한다는 이유로 format을 확장하거나 위험한 기존값 유지 동작을 넣지는 않았다.

Spec 요약: 호환성 결함 1건(P2), 명세 모순 1건. 새 원인 전달은 6개 호출 경로에 연결되어 있고, 명백한 추가 scope creep은 발견하지 못했다.

## 5. 이번 PR의 확정 신규 결함으로 분류하지 않은 항목

| 항목 | 분류하지 않은 이유·남은 확인 |
| --- | --- |
| setter와 recover 사이 전역값 경쟁 | 전역 상태는 있지만 부팅 순서는 resource→common→user다. 병렬 user loader는 같은 USER 값을 쓴다. 서로 다른 reason이 실제로 겹치는 실행 경로를 입증하지 않은 상태에서 race를 확정하지 않음 |
| recovery 상태의 오래된 index/version | 부팅 때 저장한 `g_bp_recovery_info`가 runtime BP 변경 후 오래될 수 있는 구조는 기존에도 있었음. 이번 reason 분리의 신규 결함으로 세지 않음 |
| valid BP 전체 논리 구조체 복사 | reason 보존은 아님. 미사용 app entry 등까지 남을 수 있지만, 공개 소스에서 이것이 이번 PR 때문에 잘못된 active entry 사용으로 이어진다고 확정하지 않음 |
| 모든 원인을 동시에 기록하지 않음 | 기존 check 우선순위에 따라 첫 실패를 기록한다. 복수 원인 저장은 명시된 요구가 아님 |
| 현재 CI 성공 | 기존 CI 구성의 빌드 증거. 실제 복구 및 Flash 내구성·power-loss 검증을 대체하지 않음 |

특히 기존 recovery 상태 수명 문제는 향후 런타임 업데이트·복구를 검토할 때 별도 대상으로 삼을 가치가 있다. 이번 패치에서 원인 선택 변경과 함께 광범위하게 수정하지 않았다.

## 6. 제공한 수정 코드와 적용 방법

### 6.1 파일

| 산출물 | 내용 |
| --- | --- |
| [수정 패치](pr7536/0001-preserve-bp-reason-values-and-internal-api.patch) | PR head 대비 실제 C/header 변경, 3개 파일 |
| [verify.py](pr7536/verify.py) | 고정 head에서 원본 함수를 추출하고 patch 적용·컴파일·실행 |
| [host_harness.c.in](pr7536/host_harness.c.in) | host fixture·mock 및 동작 assertion |
| [validation.json](pr7536/validation.json) | 이번 실행의 원본/수정본별 결과 |

패치의 핵심 변경은 다음과 같다.

```c
/* 영속 값의 이전 의미를 유지한다. */
BP_UPDATE_UNKNOWN = 16,
BP_UPDATE_BINARY_MANAGER_RECOVERY_COMMON = 17
```

```c
/* internal header에만 선언 */
#ifdef CONFIG_USE_BP
void binary_manager_set_bp_recovery_reason(uint8_t reason);
#endif
```

loader에서는 COMMON 사용 여부와 `bin_idx`로 지역 변수 `recovery_reason`을 한 번 계산하고, 세 실패 경로에서 동일 setter를 호출한다. 원인 enum 숫자 수정 이외의 분류 동작은 바꾸지 않았다.

### 6.2 적용 대상

**패치는 `c7a98ec37...` 위에 적용하는 후속 수정안이다. `codex/docs` 또는 임의의 최신 master에 바로 적용하는 패치가 아니다.** PR의 원본 변경 5개 파일을 포함한 누적 패치도 아니다.

PR head를 별도 checkout한 작업 트리에서 실행할 명령은 다음과 같다. `PATCH_PATH`는 실제 패치 절대 경로로 바꾼다. 이 문서 작성 중 사용자의 제품 작업 트리에 아래 apply를 실행하지 않았다.

```sh
git rev-parse HEAD
# 예상: c7a98ec37fb0240a67cd99e8b09ca2fc775db33d

git apply --check "$PATCH_PATH"
git apply "$PATCH_PATH"
git diff --check
```

검증 스크립트는 현재 docs 저장소 루트에서 다음과 같이 실행한다. Python 3, clang, git 및 로컬 PR head 객체가 필요하다. 이번 작업에서 객체는 fetch해 두었다.

```sh
python3 docs/analysis/pr7536/verify.py --output /tmp/pr7536-validation.json
```

다른 clone에서 head 객체가 없으면 먼저 `git fetch https://github.com/Samsung/TizenRT.git refs/pull/7536/head`로 가져온다. 스크립트는 고정 SHA를 사용하므로 PR이 변경되어도 당시 검토 버전을 검사한다.

## 7. 실제 수행한 검증

### 7.1 원본 함수를 사용한 host regression

검증용으로 같은 알고리즘을 새로 구현하지 않고, 고정 SHA에서 다음 함수 본문과 BP 구조체/enum을 추출해 C harness에 넣었다.

- `binary_manager_check_bootparam_set()`
- `binary_manager_is_set_mismatch()`
- `binary_manager_is_bp_kernel_address_valid()`
- `binary_manager_make_bootparam_from_partitions()`
- `binary_manager_set_bp_recovery_reason()`
- `binary_manager_open_bootparam()` / `binary_manager_write_bootparam_to_slot()`
- `binary_manager_load()`

테스트 입력은 다음을 확인한다.

| 검사 | 원본 | 수정본 |
| --- | --- | --- |
| 과거 UNKNOWN byte=16을 새 enum으로 해석 | 충돌 재현, 의도한 실패 exit 1 | UNKNOWN으로 유지, pass |
| 새 COMMON과 UNKNOWN의 분리 | 구분 계약 위반 | COMMON=17, UNKNOWN=16 |
| 정상 BP 검사 | pass | pass |
| BP 검색 실패, set 불일치, kernel 주소 불일치 | 기대 reason 선택 | 동일 동작 |
| 기존 bootloader reason 1–7 | 새 BOOTLOADER_RECOVERED 선택 | 동일 동작 |
| 기존 1–7을 새 BP에 다시 넣은 반례 | check가 다시 복구 요청 | 동일 반례 확인 |
| USER/RESOURCE/COMMON 전달과 마지막 byte 기록 | PR의 값대로 기록 | 수정된 값대로 기록 |
| loader의 서명·헤더·로드 실패 | COMMON/USER 구분 | 동일 구분 |
| BP off loader 실패 | BP 복구 API 호출 없음 | 동일 동작 |
| loader 성공 | 복구 API 호출 없음 | 동일 동작 |

### 7.2 빌드·실행 구성

원본과 패치 적용본 각각 다음 7개 **호스트 매크로 구성**을 컴파일했다.

1. BP off
2. BP on, kernel-only
3. BP on, kernel + resource
4. BP on, app
5. BP on, app + signing
6. BP on, common + app
7. BP on, common + app + resource + signing

총 14개 host executable을 만들고 각각 기능 검사와 legacy ABI 검사를 실행했다. 총 28회 실행 중 원본의 legacy 검사 7회는 **결함 재현을 위한 기대 실패**이며, 수정본은 모두 통과했다. ASan·UBSan을 활성화했고 실행 중 sanitizer 오류는 없었다.[실행 결과](pr7536/validation.json)

컴파일러는 Apple clang 21.0.0이다. `-Wall -Wextra -Werror`를 사용하되, 추출 범위의 unused mock/function 및 기존 코드에 있는 array-pointer bool 검사·signed/unsigned 비교 경고는 각각 명시적으로 제외했다. 경고를 피하려고 PR 원본 함수 본문을 바꾸지는 않았다.

패치는 빈 임시 저장소의 정확한 원본 파일에 `git apply --check` 후 적용했다. 독립 재검토에서도 setter 이동의 include 관계와 reason 계산 통합의 의미 보존에 중대한 문제를 찾지 못했다.

### 7.3 이 검증이 증명하지 않는 것

I/O, CRC 함수, partition/header 조회, loader dependency와 `binary_manager_recover_bootparam_set()`는 mock이다. writer의 바이트 배치와 loader의 원인 전달은 실제 함수를 실행했지만, A/B 전체 검증·Flash 기록·재부팅을 포함한 전체 복구 절차를 실행한 것은 아니다.

또한 macro 조합별 추출 코드 빌드는 제품의 Kconfig·링커·아키텍처 전체 빌드와 다르다. BP off 구성에도 host harness가 검사 대상으로 추출한 bootparam 함수가 들어가므로 “BP off 펌웨어를 빌드했다”는 의미로 해석하면 안 된다.

도입 전에는 아래를 추가 확인해야 한다.

- 실제 사용 보드의 BP format 1/2, COMMON/RESOURCE/signing 구성으로 전체 빌드.
- 실제 BP 파일의 reason을 기존/신규 decoder에서 해석하는 호환성.
- 각 복구 경로의 Flash 마지막 byte·CRC·BP version·선택 slot과 재부팅 후 로그.
- bootloader reason 1–7에서 한 번 복구한 뒤 다시 같은 조건으로 복구하지 않는지.
- 파티션 write 실패·전원 차단 시 이전 유효 BP로 부팅하는지.
- runtime BP update 후 복구 상태가 최신 slot/version과 일치하는지. 이는 기존 구조의 후속 검토 항목이다.

## 8. 최종 판단

이번 PR의 새 사유 연결은 구현되어 있다. 제공한 패치는 **기존 UNKNOWN 값의 호환성, 내부 API 배치, 중복된 선택 코드**를 좁은 범위에서 보완한다.

“이전 사유 보존”은 현재 구현의 성질이 아니다. **최신 원인 하나를 기록하는 기능으로 명세를 정리**하는 것을 권한다. 이전 사유도 영속적으로 필요하다면 별도의 이력 저장 설계로 다루어야 한다. 이 정책을 명확히 하고 실제 보드 검증을 마친 뒤 도입하는 것이 적절하다.

Standards: 2건, 최고 P3(규약 M09). Spec: 호환성 결함 1건(P2)과 명세 모순 1건. 수정 패치와 host 검증은 완료했으며 전체 타깃·보드 검증은 남아 있다.

[pr]: https://github.com/Samsung/TizenRT/pull/7536
[prapi]: https://api.github.com/repos/Samsung/TizenRT/pulls/7536
[checks]: https://github.com/Samsung/TizenRT/pull/7536/checks
[status]: https://api.github.com/repos/Samsung/TizenRT/commits/c7a98ec37fb0240a67cd99e8b09ca2fc775db33d/status
[files]: https://github.com/Samsung/TizenRT/pull/7536/files
[types]: https://github.com/Samsung/TizenRT/blob/c7a98ec37fb0240a67cd99e8b09ca2fc775db33d/os/kernel/binary_manager/binary_manager_internal.h#L186-L258
[enum]: https://github.com/Samsung/TizenRT/blob/c7a98ec37fb0240a67cd99e8b09ca2fc775db33d/os/kernel/binary_manager/binary_manager_internal.h#L186-L205
[baseenum]: https://github.com/Samsung/TizenRT/blob/df09bb8a19eed8e63b66a5b91ba349aefd133b20/os/kernel/binary_manager/binary_manager_internal.h#L186-L204
[mkbp]: https://github.com/Samsung/TizenRT/blob/c7a98ec37fb0240a67cd99e8b09ca2fc775db33d/os/tools/mkbootparam.py#L28-L100
[bootnote]: https://github.com/Samsung/TizenRT/blob/c7a98ec37fb0240a67cd99e8b09ca2fc775db33d/build/tools/amebasmart/bootloader/km4_boot_RELEASE_NOTE.txt#L18-L23
[check]: https://github.com/Samsung/TizenRT/blob/c7a98ec37fb0240a67cd99e8b09ca2fc775db33d/os/kernel/binary_manager/binary_manager_bootparam.c#L651-L700
[make]: https://github.com/Samsung/TizenRT/blob/c7a98ec37fb0240a67cd99e8b09ca2fc775db33d/os/kernel/binary_manager/binary_manager_bootparam.c#L464-L552
[recover]: https://github.com/Samsung/TizenRT/blob/c7a98ec37fb0240a67cd99e8b09ca2fc775db33d/os/kernel/binary_manager/binary_manager_bootparam.c#L724-L775
[write]: https://github.com/Samsung/TizenRT/blob/c7a98ec37fb0240a67cd99e8b09ca2fc775db33d/os/kernel/binary_manager/binary_manager_bootparam.c#L288-L347
[load]: https://github.com/Samsung/TizenRT/blob/c7a98ec37fb0240a67cd99e8b09ca2fc775db33d/os/kernel/binary_manager/binary_manager_load.c#L155-L332
[resource]: https://github.com/Samsung/TizenRT/blob/c7a98ec37fb0240a67cd99e8b09ca2fc775db33d/os/kernel/binary_manager/binary_manager_resource.c#L350-L440
[board]: https://github.com/Samsung/TizenRT/blob/c7a98ec37fb0240a67cd99e8b09ca2fc775db33d/os/board/rtl8730e/src/rtl8730e_boot.c#L338-L346
[public]: https://github.com/Samsung/TizenRT/blob/c7a98ec37fb0240a67cd99e8b09ca2fc775db33d/os/include/tinyara/binary_manager.h#L311-L315
[m09]: https://github.com/Samsung/TizenRT/blob/c7a98ec37fb0240a67cd99e8b09ca2fc775db33d/docs/CodingStyleGuide.md#L425-L431
