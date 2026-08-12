# QEMU ARMv8-M LTP 테스트 결과

## 최종 상태

2026-08-12 KST 기준 최종 clean 산출물에서 등록된 LTP 테스트 **46/46 PASS**다.
FAIL, unresolved, timeout, QEMU exit, crash, assertion은 모두 0건이다.

## 대상

- worktree: `/Volumes/T7/Dev/TizenRT/codex/qemu-armv8m-ltp`
- branch: `codex/qemu-armv8m-ltp`
- base: `codex/qemu-armv8m-kernel-tc` (`b5eacbdc0`)
- imported commit: `2ec68830a2283dad9ca96ce2c6ad052fb2404152` -> `33473b7ba`
- board/config: `qemu-armv8m/hello`
- QEMU: `11.0.3`, `mps2-an505` / Cortex-M33
- LTP: `20230516`, 선택된 scheduler/pthread test 46개

## clean build

```sh
cd os
./dbuild.sh distclean
./dbuild.sh qemu-armv8m hello
./dbuild.sh build
```

- 결과: 성공
- 적용 patch: 18/18, 적용 실패 무시 없음
- 생성/등록 명령: 46
- kernel size: 538,400 / 655,360 bytes (82.15%)
- `libapps.a`: 274,255 bytes (`data 316`, `bss 2,096`, `text 271,843`)
- final `tinyara` SHA-256: `0745d9facfe6032b377197b129bfb888d87477341e59bbf5db20fd8736f3897d`

## 최종 전체 실행

```sh
python3 -B .github/scripts/qemu-armv8m-ltp.py \
  --test-timeout 45 \
  --log build/qemu-armv8m-ltp/final-clean-all.log \
  --result build/qemu-armv8m-ltp/final-clean-all.result.json
```

| 항목 | 결과 |
| --- | ---: |
| selected | 46 |
| executed | 46 |
| PASS | 46 |
| FAIL/UNRESOLVED/ERROR | 0 |
| timeout/crash/QEMU exit | 0 |
| host runner exit | 0 |

- raw serial SHA-256: `f12333bc287950bcc676f1c352975664c0b0dc9cbf30a67b06087b9ecc7b9603`
- result JSON SHA-256: `c32f33b71cc7e48240edcba56e4d199057d7e007f4612cbc639a0ddbd58b0b0b`
- JSON top-level: `status=pass`, `run_error=null`, `passed=46`, `failed=0`

## 명령별 결과와 원본 source

아래 mapping은 build가 생성한 `ltp_manifest.tsv`와 최종 result JSON을 대조했다.
경로 prefix는 `testcases/open_posix_testsuite/`이다.

| 명령 | 원본 source | 결과 |
| --- | --- | --- |
| `ltp_t1` | `functional/threads/condvar/pthread_cond_wait_1.c` | PASS |
| `ltp_t2` | `functional/threads/condvar/pthread_cond_wait_2.c` | PASS |
| `ltp_t3` | `functional/threads/schedule/1-1.c` | PASS |
| `ltp_t4` | `functional/threads/schedule/1-2.c` | PASS |
| `ltp_t5` | `conformance/interfaces/pthread_attr_setschedparam/1-1.c` | PASS |
| `ltp_t6` | `conformance/interfaces/pthread_attr_setschedparam/1-2.c` | PASS |
| `ltp_t7` | `conformance/interfaces/pthread_attr_setschedparam/1-3.c` | PASS |
| `ltp_t8` | `conformance/interfaces/pthread_attr_setschedparam/1-4.c` | PASS |
| `ltp_t9` | `conformance/interfaces/pthread_attr_setschedparam/speculative/3-1.c` | PASS |
| `ltp_t10` | `conformance/interfaces/pthread_attr_setschedparam/speculative/3-2.c` | PASS |
| `ltp_t11` | `conformance/interfaces/pthread_attr_setschedpolicy/1-2.c` | PASS |
| `ltp_t12` | `conformance/interfaces/pthread_attr_setschedpolicy/1-3.c` | PASS |
| `ltp_t13` | `conformance/interfaces/pthread_attr_setschedpolicy/4-1.c` | PASS |
| `ltp_t14` | `conformance/interfaces/pthread_getschedparam/1-1.c` | PASS |
| `ltp_t15` | `conformance/interfaces/pthread_getschedparam/1-2.c` | PASS |
| `ltp_t16` | `conformance/interfaces/pthread_getschedparam/1-3.c` | PASS |
| `ltp_t17` | `conformance/interfaces/pthread_setschedparam/1-1.c` | PASS |
| `ltp_t18` | `conformance/interfaces/pthread_setschedparam/1-2.c` | PASS |
| `ltp_t19` | `conformance/interfaces/pthread_setschedparam/4-1.c` | PASS |
| `ltp_t20` | `conformance/interfaces/pthread_setschedparam/5-1.c` | PASS |
| `ltp_t21` | `conformance/interfaces/pthread_setschedprio/1-1.c` | PASS |
| `ltp_t22` | `conformance/interfaces/sched_get_priority_max/1-1.c` | PASS |
| `ltp_t23` | `conformance/interfaces/sched_get_priority_max/1-2.c` | PASS |
| `ltp_t24` | `conformance/interfaces/sched_get_priority_max/2-1.c` | PASS |
| `ltp_t25` | `conformance/interfaces/sched_get_priority_min/1-1.c` | PASS |
| `ltp_t26` | `conformance/interfaces/sched_get_priority_min/1-2.c` | PASS |
| `ltp_t27` | `conformance/interfaces/sched_get_priority_min/2-1.c` | PASS |
| `ltp_t28` | `conformance/interfaces/sched_getparam/1-1.c` | PASS |
| `ltp_t29` | `conformance/interfaces/sched_getparam/2-1.c` | PASS |
| `ltp_t30` | `conformance/interfaces/sched_getparam/3-1.c` | PASS |
| `ltp_t31` | `conformance/interfaces/sched_getparam/speculative/7-1.c` | PASS |
| `ltp_t32` | `conformance/interfaces/sched_getscheduler/1-1.c` | PASS |
| `ltp_t33` | `conformance/interfaces/sched_getscheduler/3-1.c` | PASS |
| `ltp_t34` | `conformance/interfaces/sched_getscheduler/4-1.c` | PASS |
| `ltp_t35` | `conformance/interfaces/sched_rr_get_interval/1-1.c` | PASS |
| `ltp_t36` | `conformance/interfaces/sched_rr_get_interval/2-1.c` | PASS |
| `ltp_t37` | `conformance/interfaces/sched_rr_get_interval/speculative/5-1.c` | PASS |
| `ltp_t38` | `conformance/interfaces/sched_setparam/22-1.c` | PASS |
| `ltp_t39` | `conformance/interfaces/sched_setparam/23-1.c` | PASS |
| `ltp_t40` | `conformance/interfaces/sched_setparam/25-1.c` | PASS |
| `ltp_t41` | `conformance/interfaces/sched_setparam/5-1.c` | PASS |
| `ltp_t42` | `conformance/interfaces/sched_setscheduler/16-1.c` | PASS |
| `ltp_t43` | `conformance/interfaces/sched_setscheduler/17-5.c` | PASS |
| `ltp_t44` | `conformance/interfaces/sched_setscheduler/19-5.c` | PASS |
| `ltp_t45` | `conformance/interfaces/sched_setscheduler/4-1.c` | PASS |
| `ltp_t46` | `conformance/interfaces/sched_yield/2-1.c` | PASS |

모든 entry의 result는 `exit_status=0`이다. 실행 시간은 priority wake-up을 실제로
기다리는 `t1`-`t4`와 signal stress인 `t20`이 약 1.5~1.6초였고 나머지는 각
0.001~0.002초였다.

## 최초 실패와 최종 변화

| 집합 | 최초 | 수정 후 |
| --- | --- | --- |
| `t1`-`t3` | priority wake-up FAIL | PASS |
| `t9`, `t10` | invalid priority FAIL | PASS |
| `t20` | 45초 timeout | PASS, 1.588초 |
| `t24`, `t27` | invalid policy errno FAIL | PASS |
| `t31` | unknown pid errno UNRESOLVED | PASS |
| 나머지 37개 | PASS | PASS |
| 합계 | PASS 37 / 실패 9 | PASS 46 / 실패 0 |

상세 원인과 각 수정의 근거는 `QEMU_ARMv8M_LTP_Investigation_Log.md`에 있다.

## 기존 QEMU 회귀

최종 clean LTP image에 기존 repository runner를 사용했다.

```sh
/opt/homebrew/bin/python3 .github/scripts/qemu-armv8m-kernel-tc.py \
  --config hello --timeout 600 \
  --log build/qemu-armv8m-ltp/final-clean-regression.log \
  --result build/qemu-armv8m-ltp/final-clean-regression.result.json
```

| suite | PASS | FAIL | 결과 |
| --- | ---: | ---: | --- |
| `network_tc` | 161 | 0 | PASS |
| `kernel_tc` | 459 | 0 | PASS |

- regression raw serial SHA-256: `b11d12e9ebd7df013a15291e33202949f55efdf7aa9ed1050eb1b0aef8d53ec5`
- regression result JSON SHA-256: `cc4353f7bfc63879eca47c55bb56a8afed7980414dabefd20c4f6f1f19b4b82e`

## 추가 검증

```sh
python3 .github/scripts/tests/test_qemu_armv8m_ltp.py -v
/opt/homebrew/bin/python3 .github/scripts/tests/test_qemu_armv8m_ltp.py -v
/opt/homebrew/bin/python3 -m py_compile \
  .github/scripts/qemu-armv8m-ltp.py \
  .github/scripts/tests/test_qemu_armv8m_ltp.py
sh -n apps/examples/ltp/ltp_register.sh
git diff --check
```

- host runner unit test: system Python 3.9.6과 Homebrew Python 3.14.6에서 각각 4/4 PASS
- generated manifest/registry, raw serial, JSON: 46개 command/function/source/result 정확히 일치
- Python compile, POSIX shell syntax, whitespace check: PASS

## 검증 경계

이 결과는 ARM64 Docker toolchain과 QEMU Cortex-M33 software model에서 확인했다.
실제 board, 다른 host/toolchain, 선택 범위 밖의 LTP test는 검증하지 않았다.
serial/JSON은 재현용 local build artifact로 남기고 Git에는 Markdown 결과만 보존한다.
