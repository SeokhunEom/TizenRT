# TizenRT QEMU 진단 로그 corpus

`qemu-armv8m/log_analyzer` 전용 recipe에서 커널 진단을 실제로 발생시키고
원시 serial 출력과 실행 증거를 함께 수집한다. 호스트 프로세스나 실제 보드에
fault를 주입하지 않는다. `CONFIG_QEMU_LOG_ANALYZER_PROBES` 기본값은 꺼짐이며
기존 `hello`, loadable, XIP recipe에는 `crashprobe`가 등록되지 않는다.

## 빌드와 실행

저장소의 [AI 빌드 규칙](../../AGENTS.md)을 따른다. 이 recipe는 flat
MPS2-AN505/Cortex-M33용이다.

```sh
cd os
/opt/homebrew/bin/bash ./dbuild.sh menu
# 기존 설정이 있다면 5: Clean Build and Re-Configure
# qemu-armv8m → log_analyzer → 1: Build with Current Configuration
# 빌드 성공 후 x로 메뉴 종료 (메뉴의 x 자체는 exit code 1).
cd ..
python3 tools/crash-corpus/collect.py --out /tmp/tizenrt-crash-run-001

# 하나의 케이스만, 항상 새 evidence directory를 지정한다.
python3 tools/crash-corpus/collect.py --out /tmp/tizenrt-crash-run-002 --case divzero

# 긴 kernel_tc 회귀는 명시적으로 실행한다. 기본 23개 케이스에 포함하지 않는다.
python3 tools/crash-corpus/collect.py --out /tmp/tizenrt-kernel-run-001 \
  --case kernel_tc --timeout 300
```

각 케이스마다 새 QEMU를 시작하고 새 TASH prompt 및 명령 등록 후 입력한다.
실행 명령은 case JSON에, QEMU 버전과 ELF/config SHA-256은 provenance에 기록된다.
출력 폴더가 이미 있으면 중단하여 기존 증거를 덮어쓰지 않는다. `.config`가 ELF보다
새로우면 재빌드를 요구한다. ELF 자체는 `build/output/bin/tinyara`에 보존하고,
corpus에는 hash/path와 `System.map`을 보존한다. ELF를 교체하기 전에 필요한
파일을 별도로 보관해야 이후 정확한 source symbolization이 가능하다.

## 확인한 실제 로그

[`evidence/20261016-qemu-armv8m`](evidence/20261016-qemu-armv8m/summary.json)의
23개 케이스에서 기대한 진단 문자열을 실제 관측했다. 로그는 합성 예시가 아니다.

같은 ELF로 별도 실행한
[`20261016-kernel-tc`](evidence/20261016-kernel-tc/summary.json)는 128.29초 동안
`Kernel TC End [PASS : 459, FAIL : 0]`을 출력했다. 해당 실행에는 assertion,
lockup, timeout이 없었다. `network_tc`와 실제 하드웨어 회귀 결과는 포함하지 않는다.

| 케이스 | 관측한 진단 |
|---|---|
| boot, ps, free | 부팅 정보, task/wait 상태, 메모리 합계 |
| heapinfo, heapinfo_all, heapinfo_free, heapinfo_regions | heap 요약, allocation list, free list, region, fragmentation |
| stackmonitor | 살아 있는/종료한 thread의 peak stack/heap |
| network_interfaces | loopback/eth0 인터페이스 상태; 외부 통신 검증 아님 |
| histories | task scheduling, IRQ, semaphore history ring |
| assert | 의도적 ASSERT, source/line/task, stack/TCB/register/MPU/heap |
| udf, divzero, unaligned, invstate | UNDEFINSTR, DIVBYZERO, UNALIGNED, INVSTATE |
| busfault | PRECISERR, BFAR=0x60000000, CFAULTS=0x00008200 |
| hardfault | UsageFault 비활성 상태에서 UDF가 강제 HardFault로 승격 |
| memfault, execfault | MPU read-only 영역 write의 DACCVIOL, execute-never 영역의 IACCVIOL |
| stack | 재귀 호출의 STKOF 및 뒤따른 heap corruption 진단 |
| oom | 32 MiB 요청 실패, largest/total free, allocator assertion |
| doublefree | 실제 allocator의 double free 경고와 1st/2nd free caller |
| heapcorrupt | 할당 header size 손상, forward/backward 검사, node/memory dump |

기존 23개 corpus의 `stackmonitor`는 표 1회를 관측한 뒤 prompt 조건으로 종료했고,
metadata에는 `diagnostic-complete`로 기록되어 있다. 당시 collector의 의도한
2.5초 관측 창을 충족한 결과로 해석하지 않는다. 해당 분기를 수정한 별도
[`20261016-stackmonitor-window`](evidence/20261016-stackmonitor-window/summary.json)는
명령 전송 후 2.518초 동안 표 3회를 관측하고 `diagnostic-observation-window`로
종료했다. 이전 evidence는 덮어쓰지 않았으며 각 `source/collect.py`는 당시 구현에
고정되어 있다.

`assert`부터 `heapcorrupt`까지의 fatal 케이스 12개는 진단 이후 guest reboot와
새 TASH prompt를 관측했다. `doublefree`는 경고 후 정상 명령 복귀다. collector는
출력이 멈춘 뒤 자기 QEMU 프로세스에 SIGTERM을 보내므로 최종 process return code
0이 아닌 것만으로 guest failure를 판정하면 안 된다. JSON의 다음 항목을 구분한다.

- `expected_observed`: 해당 로그 형식이 실제 관측되었는지
- `observation_timed_out`: 관측 시간 초과인지
- `guest_reboot_requested` / `guest_prompt_after_fault`: guest reset 요청/복귀 증거
- `qemu_returncode_before_collector_stop` / `collector_stopped_qemu`: 자체 종료와 수집 종료의 차이
- `architectural_lockup_observed`: QEMU의 명시적 lockup 메시지 존재 여부

이 corpus의 STKOF에는 heap corruption도 출력된다. 최초 fault와 뒤따른 진단을
함께 보존하며, 로그만으로 stack 경계/handler stack/heap 손상 중 하나를 유일한
근본 원인으로 단정하지 않는다. 실제 kernel source 위치와 동일 ELF 확인이 필요하다.

## 증거와 한계

각 run의 `kernel.config`, `provenance.json`, `build.log`, `source.patch`, `source/`
snapshot으로 dirty build의 입력을 기록했다. `build-environment.json`은 실제 빌드
Docker 이미지에서 확인한 GCC 10.3.1 및 GNU binutils 2.36.1이다. guest의 Commit Hash는
Docker bind mount에서 Git metadata를 찾지 못하여 `NA`이므로 source identity는
host provenance의 Git HEAD와 input hash를 사용한다. synthetic fixtures와 섞지 않는다.

이번 QEMU 실행은 ARMv8-M flat build의 지정된 23개 경로만 검증한다. ARMv7-M,
ARM-A/R, RISC-V, Xtensa, protected/loadable binary recovery, real watchdog/reset,
Realtek SPIC/XIP bus stall, UART/hardware timings는 이 corpus로 검증하지 않았다.
binary-manager/logdump/task-monitor/board-specific 진단은 플러그인의 source catalog에
기록할 수 있지만 실제 실행 검증으로 취급하지 않는다. `net_stats`는 현재 recipe에서
상세 통계 출력이 없어 인터페이스 진단만 채택했다. 전체 log coverage와 kernel_tc
통과도 별도 주장이다.

`verify.py`는 raw byte hash, config/ELF hash 형식, input snapshot 및 expected pattern을
다시 확인한다. QEMU를 재실행하거나 누락된 하드웨어 검증을 대신하지 않는다.

```sh
python3 tools/crash-corpus/verify.py tools/crash-corpus/evidence/20261016-qemu-armv8m
python3 tools/crash-corpus/verify.py tools/crash-corpus/evidence/20261016-kernel-tc
python3 tools/crash-corpus/verify.py tools/crash-corpus/evidence/20261016-stackmonitor-window
python3 -m unittest discover -s tools/crash-corpus -p 'test_*.py' -v
```

수집 후 heap 표 인식 조건을 강화하여 명령 echo/미등록 오류만으로 진단 성공이
되지 않도록 했다. 위 recognition test는 모든 명령의 negative 입력과 실제
23개 로그를 강화한 조건으로 다시 확인한다. `source/collect.py`는 각각의
수집 시점 코드를 보존하므로 현재 collector와 일부 인식 조건이 다를 수 있다.
현재 crashprobe C 파일은 검증 후 근거 없는 신규 기업 저작권 표기를 제거하고
Apache-2.0 SPDX 표기를 유지했다. 이는 주석만의 변경으로, 빌드 당시 source
snapshot과 ELF를 재작성하지 않았다.
