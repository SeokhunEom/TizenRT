# Health Monitor 임시 QEMU 통합 검증

2026-09-20 KST. 임시 통합 및 로컬 검증을 완료했다. 커밋·push·CI 실행은 하지 않았다.

## 기준과 변경 범위

- 대상: `/Volumes/T7/Dev/TizenRT/codex/qemu-build-test`, `codex/qemu-build-test`, `128a9ee1db00d822b144a9ef8e883fd3a3fbc318`.
- 기능 원본: `codex/260901-health-monitor`, `8d10c93bd79c69fb2b64ab420111a415e3d17eeb`. 원본 작업 폴더는 변경하지 않았다.
- 두 작업 폴더 모두 시작 시 tracked/untracked 변경이 없었다.
- 기능의 1~7단계 `os` 코드와 `apps/examples/health_monitor`를 가져왔다. 선행 legacy task_monitor 제거 커밋 및 다른 보드의 defconfig 변경은 포함하지 않았다. 기존 task_monitor는 QEMU에서 비활성이다.
- 기존 QEMU 브랜치의 pthread/mutex 복구 처리를 유지하면서 `task_recover()`에 Health Monitor cleanup을 추가했다.
- QEMU board fixture에서 `/dev/health_monitor`를 등록하고, `CONFIG_HEALTH_MONITOR=y` 및 `CONFIG_EXAMPLES_HEALTH_MONITOR=y`를 켰다.
- [임시 실행 스크립트](../../tools/qemu-build-test/health-monitor-smoke.py)를 추가했다. 정상/만료 시험은 별도 부팅이다. 의도적인 PANIC 안내 문구는 실패로 오인하지 않으며, 실제 assertion은 Health Monitor의 정확한 파일/줄만 허용한다.

## 실행 환경

- LM3S6965EVB / Cortex-M3 / 단일 코어 / flat / QEMU 16 MiB 패치 / 설정 tick 10ms / MAX_TASKS=16. 실제 QEMU tick 주기와의 차이는 아래에 기록했다.
- PM, IRQ watchdog, reboot-reason 기록은 비활성이다. PANIC 정책은 system halt다.
- Docker image: `sha256:8f2d15b7d82cf8c58a9092ec0dcc1ed1bbda9721a6cf19cc832c4eb9a48f8496`.
- ARM GCC 10.3.1, QEMU 2.12.0, Python 3.5.2. 실행 컨테이너는 소스를 읽기 전용으로 마운트하고 네트워크를 끈다.
- `make distclean` → `configure.sh qemu/build_test` → `make -j4` → defconfig/effective config 일치 → ELF 및 함수 심볼 확인을 완료했다.

## 실제 QEMU 결과

| 검증 | 결과 |
|---|---|
| 부팅, 장치 및 TASH 명령 등록, 감시 미등록 idle | 통과 |
| 잘못된 앱 입력 6종 | usage 출력 및 task 종료 확인 |
| UP에서 `smp` 요청 | 명시적인 2 CPU 요구 오류 확인 |
| `health_monitor run 2000 250 20` | 3회 통과; 회차마다 완료 후 2.2초 추가 생존. 커널 회귀 후 1회 추가 통과 |
| `health_monitor stop 2000` | 3회 통과; 기존 deadline을 지난 2100ms까지 앱에서 생존 확인 |
| `health_monitor exit 5000 100` | 3회, 총 300개 등록 worker의 종료·join 및 마지막 deadline 이후 생존 확인 |
| task/heap 관찰 | 회차마다 동일한 8개 task 복귀. heap used: 76096 / 76096 / 76096 bytes. warmup 이후 증가 0 |
| 기존 `kernel_tc` | 433 PASS / 0 FAIL. 이후 Health Monitor 정상 KICK 명령 재실행 통과 |
| `health_monitor expire 1000` | 별도 부팅에서 Health Monitor의 PANIC 위치 확인 |
| `health_monitor expire 2000` | 별도 부팅에서 동일한 PANIC 위치 확인 |

최초 1초 만료 실행은 실제 PANIC에 도달했지만, ARMv7-M의 공통 assertion 문구와 상세 문구의 두 줄 형식을 반영하지 못한 판정기 때문에 실패로 기록됐다. 해당 원시 증거는 `expire-1000/`에 보존했다. 판정기를 수정해 공통 문구 1개와 정확한 상세 위치 1개, SysTick IRQ 15, heap corruption 없음까지 확인하고 새 출력 디렉터리에서 재실행했다. 펌웨어는 변경하지 않았다. 정상 시험은 최초 판정기로 실행했고, 최초 runner hash/패치는 `source-manifest-initial-runner.json`과 `temporary-integration-initial-runner.patch`에 남겼다.

만료 시험은 `health_monitor/health_monitor.c:583`의 `PANIC()`에서 실제 assertion 로그가 나왔고 명령이 반환하지 않았음을 확인했다. 앱이 출력하는 “expect PANIC/reboot reason 62” 안내만으로 성공 판정하지 않는다. QEMU 프로세스는 증거 수집 후 테스트 도구가 종료했다. **자동 리셋이나 원인 62의 저장·읽기는 검증하지 않았다.**

이번 300회에서 PID 재사용은 관찰되지 않았다. TCB 주소 재사용도 별도로 계측하지 않았다. 메모리 수치는 관측 구간의 안정성 증거이며 모든 누수의 부재를 의미하지 않는다.

## 확인된 기존 QEMU 시간 기준 문제

1000ms/2000ms 만료는 host wall clock으로 각각 약 3.983초/7.988초 뒤 관측됐다. 별도 읽기 전용 QEMU monitor 시험에서 **변경 전 ELF와 통합 ELF의 RCC/RCC2/SysTick 값이 동일**함을 확인했다.

- RCC `0x078e3ac0`, RCC2 `0x07802810`, SysTick RELOAD `0x0007a11f`(499999), CTRL `0x00010007`.
- QEMU 2.12.0의 `ssys_calculate_system_clock()`은 이 RCC에 대해 80ns(12.5MHz)를 적용한다. SysTick 주기는 `(499999 + 1) × 80ns = 40ms`다.
- 펌웨어는 board.h의 50MHz를 사용해 reload를 계산하고 OS tick을 10ms로 간주한다. 따라서 이 구성에서 OS 시간은 QEMU 시간의 1/4 속도로 진행한다.
- 이번 시험은 감시·해제·종료·만료 경로의 기능 검증이다. **요청한 timeout의 실제 경과 시간 정확도를 통과 처리하지 않는다.** 이 기존 클록 불일치는 이번 임시 Health Monitor 통합에서 수정하지 않았다.
- [계산 및 레지스터 비교](/private/tmp/tizenrt-health-monitor-qemu-20260920-qziwrfv1/clock-analysis.json), [기존 ELF 값](/private/tmp/tizenrt-health-monitor-qemu-20260920-qziwrfv1/clock-probe-baseline/monitor.log), [통합 ELF 값](/private/tmp/tizenrt-health-monitor-qemu-20260920-qziwrfv1/clock-probe-integrated/monitor.log)를 보관했다. 최초 `xp` 물리 주소 조회에서 SysTick private mapping이 0으로 읽힌 자료는 `clock-probe/`에 남겼고, 위 판단은 CPU 주소 공간 `x` 조회를 사용한다.

Health Monitor 3회 반복 구간의 heap used는 76,096바이트로 동일했다. 커널 TC 실행 후에는 77,824바이트였다. 커널 TC 자체는 반복 실행하지 않았으므로 그 구간의 누수 여부까지 판정하지 않았다.

## 보완 호스트 검증 및 빌드 기록

- 같은 통합 소스의 `make -C os/kernel/health_monitor/tests test`를 Linux AArch64 컨테이너에서 실행했다.
- 기존 21개 UP/SMP 변형 및 watchdog 설정 거부 검사를 통과했다. ASan/UBSan 오류는 없었다. registry, ioctl/VFS, 시간 경계·wraparound, timer, PM, watchdog, 앱 fixture를 포함한다.
- 호스트 SMP/PM/watchdog 테스트는 대체 clock/IRQ/board fixture를 사용한다. 실제 QEMU 멀티코어·RTL8730E 보드 검증과 구분한다.
- ELF `text/data/bss`: 기존 `2443118/1512/2372900` → 임시 통합 `2446174/1512/2373044` bytes. 예제와 QEMU 등록 코드까지 포함한 비교다.
- Health Monitor Cortex-M3 객체의 undefined symbol에는 atomic helper가 없다. 객체와 disassembly를 보관했다.
- 빌드 로그에 C++의 C 전용 옵션 관련 진단 2줄이 있다. 기존 `tmp/qemu-build-test-stage07/ci-04/build.log`에도 같은 진단이 있다. 이번 make 종료 코드는 0이고 새 ELF 링크 및 위 runtime 검증을 완료했으며, 경고/진단이 없는 빌드라고 주장하지 않는다.

## 미검증 범위

RTL8730E의 실제 SMP 경쟁, 절전·wakeup, HW watchdog fallback, 실제 리셋 및 reboot reason 유지, 보드 ISR 비용은 미검증이다. 기존 QEMU 전체 full-set 및 원격 CI를 실행한 결과도 아니다.

## 재현 및 증거

원본 ELF·설정은 변경 전 별도 백업했다. 이번 증거와 재현 명령은 다음 경로에 있다.

- [전체 요약](/private/tmp/tizenrt-health-monitor-qemu-20260920-qziwrfv1/summary.json)
- [빌드 결과](/private/tmp/tizenrt-health-monitor-qemu-20260920-qziwrfv1/build.json), [빌드 로그](/private/tmp/tizenrt-health-monitor-qemu-20260920-qziwrfv1/build.log), [빌드 명령](/private/tmp/tizenrt-health-monitor-qemu-20260920-qziwrfv1/build.sh)
- [정상·커널 결과](/private/tmp/tizenrt-health-monitor-qemu-20260920-qziwrfv1/normal-01/result.json), [직렬 로그](/private/tmp/tizenrt-health-monitor-qemu-20260920-qziwrfv1/normal-01/serial.log)
- [1초 만료 결과](/private/tmp/tizenrt-health-monitor-qemu-20260920-qziwrfv1/expire-1000-02/result.json), [2초 만료 결과](/private/tmp/tizenrt-health-monitor-qemu-20260920-qziwrfv1/expire-2000/result.json)
- [호스트 결과](/private/tmp/tizenrt-health-monitor-qemu-20260920-qziwrfv1/host.json), [호스트 로그](/private/tmp/tizenrt-health-monitor-qemu-20260920-qziwrfv1/host.log)
- [검증한 소스 SHA-256 목록](/private/tmp/tizenrt-health-monitor-qemu-20260920-qziwrfv1/source-manifest.json), [임시 변경 패치](/private/tmp/tizenrt-health-monitor-qemu-20260920-qziwrfv1/temporary-integration.patch)
- [변경 전 펌웨어·설정](/private/tmp/tizenrt-health-monitor-qemu-20260920-qziwrfv1/baseline), [시험 펌웨어·설정](/private/tmp/tizenrt-health-monitor-qemu-20260920-qziwrfv1/firmware)

시험 결과의 ELF/config hash가 빌드 산출물과 일치하고, 검증 중 펌웨어 소스가 바뀌지 않았음을 확인했다. 임시 변경 패치는 `git apply --reverse --check`를 통과했다. 실제 되돌리기는 하지 않았으며, 나중에 되돌릴 때 현재 변경의 소유권과 manifest를 먼저 확인해야 한다. 이 보고서 자체는 시험 소스 manifest/패치에 포함하지 않는다.

```sh
HM_ROOT=/Volumes/T7/Dev/TizenRT/codex/qemu-build-test
HM_IMAGE=sha256:8f2d15b7d82cf8c58a9092ec0dcc1ed1bbda9721a6cf19cc832c4eb9a48f8496
python3 "$HM_ROOT/tools/qemu-build-test/health-monitor-smoke.py"   --root "$HM_ROOT" --output /tmp/hm-normal-new --image "$HM_IMAGE" --mode normal
python3 "$HM_ROOT/tools/qemu-build-test/health-monitor-smoke.py"   --root "$HM_ROOT" --output /tmp/hm-expire-new --image "$HM_IMAGE" --mode expire --expire-ms 2000
```

`--output`에는 존재하지 않거나 빈 디렉터리를 지정한다. 스크립트는 현재 `build/output/bin/tinyara`를 실행하므로 소스 변경 후에는 먼저 다시 빌드해야 한다.
