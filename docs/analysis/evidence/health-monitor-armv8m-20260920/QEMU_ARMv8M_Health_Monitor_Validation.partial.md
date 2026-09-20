# Health Monitor ARMv8-M QEMU 검증 — 접근 오류 시점 기록

2026-09-20. 전체 검증 미완료. 커밋·stage·push는 수행하지 않았다.

## 현재 차단 상태

진행 중 `/Volumes/T7/Dev/TizenRT/codex/260901-health-monitor`와 `qemu-armv8m-kernel-tc`의 읽기·쓰기가 모두 `Operation not permitted`로 거부되기 시작했다. 일반 실행과 권한 확장 실행에서 동일하다. `.git` 파일 읽기도 실패하며 Git은 `Unable to read current working directory`를 출력한다. 원인을 Git 충돌이나 소스 실패로 해석하지 않는다. 사용자에게 외장 볼륨/Codex 파일 접근 복구를 요청한 상태다.

Health Monitor 브랜치에 `docs/analysis/QEMU_ARMv8M_Health_Monitor_Validation.md` 초안을 작성했지만 최신 결과는 접근 차단 때문에 갱신하지 못했다. 이 파일은 접근 가능한 `/private/tmp`에 남기는 최신 중간 보고서다.

## 기준선과 소스 작업

- QEMU 시작 HEAD: `b5eacbdc0426df9ca0784dc5198ac8ff30443631` (clean).
- Health Monitor 시작 HEAD: `eddceb9665e302a32e78742359669ed4ee1c8e79` (clean).
- QEMU에 기존 Health Monitor 기능 패치를 임시 적용. 기존 reboot_reason 헤더 문맥만 달라 reason 62를 수동 추가했다. QEMU 보드 초기화에 장치 등록을 연결하고 네 recipe에 Health Monitor를 켰다.
- 두 작업 폴더에 LM3S/MPS2 flat 시험 설정을 공유하도록 guard·tick 변환 기대값을 수정. INT32_MAX, INT32_MAX+1, UINT32_MAX의 경계와 실제 worker 최대 128개 시험을 작성했으나 아직 빌드·실행하지 못했다.
- `apps/examples/health_monitor_armv8m`에 kernel observer/실제 Binary Manager recovery 진입점을 작성했다.
- `loadable_apps/health_monitor/health_monitor_user.inc`에 public open/ioctl/errno, 공유 fd, 1000회 등록·kick·stop, pthread 취소·종료, 등록 중 main return/만료, 등록된 자식 4개와 main 재로딩 시험을 작성했다. QEMU의 wifiapp/micomapp에 시험 설정으로만 활성화되는 진입점을 연결했다. 아직 빌드·실행 전이다.
- 실행기: `tools/qemu-armv8m-health-monitor/run.py`. 기존 ARMv8-M runner의 보드 명령과 A/B 패키지 staging을 재사용한다. 최신 Python 문법 검사는 통과했으나 새 protected 시나리오는 실행 전이다.

## 환경

- MPS2-AN505 / Cortex-M33 / UP / TIMER0 IRQ 19 / 설정 tick 1ms.
- ARM64 빌드 이미지: `sha256:16b314997d3ed7901fe711ff85e1cd8e90885f44bb3d683fc7040699ebe2b74c`.
- GCC 10.3 계열 이미지, 호스트 QEMU 11.1.1, Homebrew Python 3.14 실행기.
- Docker 데몬이 중간에 종료되어 최초 빌드가 컴파일 전 실패했다. GUI/desktop CLI 시작이 원활하지 않았고 백엔드 시작 후 기존 이미지 접근과 빌드가 복구됐다. Docker 설치·이미지 변경은 하지 않았다.
- macOS 기본 Python 3.9 호출은 ARMv8-M runner의 타입 문법과 호환되지 않았다. Python 3.10 이상 가드를 추가하고 Homebrew Python으로 실행했다.

## 완료된 실행과 판정

| 실행 | 결과 | 증거 |
|---|---|---|
| hello Health Monitor on / time probes off clean build | PASS, 39.660초 | [빌드 결과](hello-production-build-retry/result.json), [빌드 로그](hello-production-build-retry/build.log) |
| 기본 KICK, STOP, worker 10회 종료 | PASS | [결과](hello-basic-retry/result.json) |
| 정상 예제 3회차 및 300회 worker 종료 후 kernel_tc | 커널은 459/0, 그러나 이후 TASH `ps` 응답 확인 실패. 전체 실행 FAIL | [결과](hello-normal/result.json), [직렬 로그](hello-normal/serial.log) |
| 표준 network/kernel 회귀 — 기본 샌드박스 | DNS 확인 실패 후 IPv6 재시도 실패. 환경 오류 | [결과](hello-regression.json) |
| 표준 network/kernel 회귀 — DNS 가능한 권한 확장 실행 | PASS. Network 161/0, Kernel 459/0 | [결과](hello-regression-unrestricted.json), [직렬 로그](hello-regression-unrestricted.log) |
| 1000ms 만료/reset — 첫 실행 | 정확한 Health Monitor PANIC 및 IRQ19 도달. 1.5125초로 시간 허용 오차 초과, FAIL | [결과](hello-reset-1000/result.json) |
| 1000ms 만료/reset — 독립 재실행 | 1.5050초로 동일한 시간 불일치, FAIL | [결과](hello-reset-1000-isolated/result.json) |

위 실행의 ELF SHA-256: `ebf80047a5ab95d0717a88378c225b36f1b73927bcd69f3cba0aabde1d1c6004`.

표준 회귀 PASS는 suite 종료 집계까지의 증거다. 같은 부팅에서 이후 TASH 명령까지 정상이라는 증거로 확장하지 않는다. 1초 만료 시험은 시간 검사 실패 후 종료되어 reset 최종 판정이 완료되지 않았으며, HW reset/reboot reason 유지의 성공으로 기록하지 않는다.

## 타이머 추가 진단

T7 접근이 막힌 뒤 `/private/tmp`에 보존한 같은 ELF로 수행했다. source 변경은 없다.

- 실제 TIMER0 CTRL=`0x9`, RELOAD=`0x4e20`(20000). QEMU `info qtree`의 pclk=20MHz. 설정값상 약 1ms 주기다.
- HMP로 `g_system_timer`를 관찰했을 때 1000 OS ticks가 호스트 시간 약 1.5046초에 해당했다. [관찰](hello-clock-observation/result.json)
- 디버거로 사용하지 않는 TIMER1을 20MHz free-running counter로 설정한 후 비교했다. 약 2.031844초의 TIMER1 진행 동안 OS tick은 1352회 증가했다. 비율은 약 0.6654 ticks/ms다. [기본 실행 비교](hello-clock-reference-gdb-start/result.json)
- 같은 ELF를 `-icount shift=auto,align=off,sleep=on`으로 실행하면 TIMER1 기준 약 1.833354초에 1832 OS ticks로 약 0.9993 ticks/ms였다. [icount 비교](hello-clock-icount/result.json)
- 이는 클록 주파수 상수 불일치보다는 기본 QEMU 실행에서 주기 인터럽트가 전달·처리되는 방식과 tick 계수의 관계를 우선 조사할 근거다. 아직 Health Monitor off 기준선 비교나 소스 수정 후 검증은 없으므로 최종 원인/수정 완료로 단정하지 않는다. icount의 호스트 시간 진행은 불균일하여 이를 실시간 만료 PASS로 취급하지 않는다.
- `hello-clock-reference`의 loader를 이용한 MMIO 초기화 시도는 TIMER1 값이 0이라 무효 진단이었다. `hello-clock-reference-gdb`는 실행 중 첫 GDB 연결의 정지 응답 timeout으로 실패했다. 위의 `gdb-start` 결과만 유효한 보조 타이머 비교다.

QEMU 모델 확인에 참고한 1차 소스: [AN505 클록](https://github.com/qemu/qemu/blob/master/hw/arm/mps2-tz.c), [CMSDK timer 모델](https://github.com/qemu/qemu/blob/master/hw/timer/cmsdk-apb-timer.c), [TIMER0/1 연결](https://github.com/qemu/qemu/blob/master/hw/arm/armsse.c). 설치 버전의 실제 주파수는 `qtree.txt`에서 별도로 확인했다.

## 복구 후 남은 작업

1. 두 브랜치의 HEAD/index/현재 변경을 다시 확인한다. 커밋하지 않는다.
2. 1ms timer 계수 불일치와 kernel_tc 이후 TASH 응답을 Health Monitor off 기준선과 비교한다. 검증기의 prompt/echo 가정과 포트 문제를 분리한다.
3. 필요하면 timer/runner를 수정한 뒤 실제 시간·논리 tick·reset을 각각 재검증한다.
4. hello 확장 flat 시험(all 3회차, fatal 10종, 별도 capacity=4, 128 worker 구성)을 빌드·실행한다.
5. loadable_all/loadable_apps/xip_all에서 사용자 앱 공개 API 시험을 실행한다. 재로딩 검증용 설정은 BINMGR_RELOAD_REBOOT=n, BINMGR_RECOVERY=y가 필요하다.
6. 실제 Binary Manager deactivate/unload/reload 후 등록 5개 제거, 옛 PID 소멸, deadline 이후 생존, 새 앱 상태·task/heap 안정성을 확인한다. 현재 user-reload 실행기는 이를 검사하도록 작성만 된 상태다.
7. 세 protected recipe의 기존 회귀, 호스트 21종, 최종 probe-off ELF/설정/소스 hash를 수집한다.
8. 최신 결과·로그·설정·정확한 source patch/manifest를 Health Monitor 브랜치 보고서에 보관하고 미커밋 상태로 마무리한다.

## 미검증 경계

SMP/캐시 순서, 실제 PM 절전·wakeup, RTL8730E HW watchdog/reset/reboot reason 유지, 물리 보드 ISR 비용, 64비트 unsigned-long 경로, 원격 CI는 검증되지 않았다.
