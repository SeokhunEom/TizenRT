# QEMU build_test: 6단계 통합 반복 검증

검증일: 2026-09-18 KST. 브랜치: `codex/qemu-build-test`.
기준은 5단계 커밋 `3533019f4792376ca0f47e4a876f2d0706e219c0`다.

## 범위와 판정

`qemu/build_test`의 ARMv7-M / Cortex-M3 Flat 구성에서 C++·libc++·SmartFS·network·kernel·drivers를 같은 QEMU 부팅 안에서 세 번 연속 실행한다. defconfig는 5단계와 동일하다. CircleCI 연결은 다음 7단계 범위다.

`tools/qemu-build-test/full-set.py`가 매 회 다음 순서로 실행한다.

1. loopback TCP/UDP, eth0 재기동, DHCP, ping, 격리된 host와 양방향 TCP/UDP binary payload 검증.
2. `network_tc`, `helloxx`, `cxxtest`, `libcxx_utc`.
3. `filesystem_tc`, SmartFS unmount/remount 및 sentinel 확인, `smart_test`, `smart` 100회 fill/delete.
4. `drivers_tc`, `kernel_tc`.

매 workload 종료 후 기존 task PID 집합으로 돌아왔는지 확인하고 heap의 total/used/free/largest를 기록한다. 기존 kernel TC는 PID 0의 이름을 `thread_waiter`로 바꾸므로 이름 변경과 task 누수를 구분한다. `/mnt` sentinel 내용과 mount 집합(`/mnt` SmartFS, `/proc` procfs, `/tmp` tmpfs)을 매번 검사한다. 각 회차 끝에는 `df`와 `/mnt`, `/tmp`의 recursive 파일명·크기도 저장한다.

두 번째 full round를 warmup 이후 기준으로 삼아 마지막 회차의 heap used가 증가하지 않고 파일 목록·크기·filesystem 용량이 같은지 검사한다. 이는 관측 구간의 안정성 판정이다. 첫 두 회차에서 남은 할당이나 모든 종류의 누수가 없다는 증명은 아니며, largest 값만으로 단편화가 없다고 주장하지 않는다.

PWM/watchdog/ADC 미지원에 따른 기존 driver TC 8개 실패는 `known-driver-failures.json`에 함수명·실패 이유·장치 부재로 고정했다. 정확히 동일한 경우만 `known_failures`로 분류한다. raw 집계는 계속 14 PASS / 8 FAIL이며 다른 실패, 중복, 종료 누락은 전체 실패다. 정상 최종 상태도 `pass_with_known_driver_failures`로 표현한다. `--omit-kernel` 또는 3회 미만 실행은 `diagnostic_pass`일 뿐 full-set 완료로 취급하지 않는다.

## 통합 실행에서 발견한 문제

### eth0 활성화 후 TCP negative test 정지

첫 통합 진단은 host 송수신 뒤 `network_tc`에서 멈췄다. GDB로 broadcast destination의 TCP PCB가 `SYN_SENT`에 남는 것을 확인했다. loopback negative test에도 `INADDR_LOOPBACK`을 network byte order로 바꾸지 않은 문제가 있어 실제 목적지가 `1.0.0.127`이 됐다.

- loopback 주소에 `htonl()`을 적용했다.
- broadcast negative case를 nonblocking으로 실행하되 즉시 `EINVAL`을 요구하도록 했다. `EINPROGRESS`를 성공적인 거절로 오인하지 않는다.
- IPv4/IPv6 multicast negative case도 추가했다.
- lwIP `tcp_connect()`는 PCB 변경이나 SYN 송신 전에 multicast와 IPv4 limited/directed broadcast 목적지를 거절한다. directed broadcast 검사는 실제 송신과 같은 local-source-aware `ip_route()` 결과를 사용한다.

이는 invalid remote IP(broadcast/multicast)에 대한 active OPEN 거절 요구에 부합한다. [RFC 9293 §3.9.1.1, MUST-46](https://datatracker.ietf.org/doc/html/rfc9293#section-3.9.1.1)

실제 QEMU regression은 수정 전 160 PASS / 3 FAIL, 수정 후 163 PASS / 0 FAIL이다. limited broadcast, IPv4 multicast, IPv6 multicast의 즉시 거절은 실행으로 확인했다. directed broadcast와 bound-source routing 분기는 소스 검토 결과이며 별도 runtime 재현과 구분한다.

### libc++ 두 번째 실행 실패

다음 진단은 첫 libc++ 실행 795 PASS / 0 FAIL, 두 번째 794 PASS / 1 FAIL이었다. libc++만 연속 실행해도 `unique_lock::release()` 테스트의 `mutex::lock_count == 1` assertion이 같은 방식으로 실패했다.

해당 테스트는 계측용 static 카운터를 사용하지만 시작 시 초기화하지 않았다. 통합 runner에서 같은 함수를 다시 호출하면 누적된다. 테스트 진입 시 lock/unlock 카운터를 초기화했다. 실제 OS mutex 상태나 원래 assertion을 완화하지 않는다. 이 테스트는 순차 실행한다. 수정 후 libc++ 단독 검사는 같은 부팅에서 세 번 모두 795 PASS / 0 FAIL이었고, 매번 종료 후 heap used는 76,000바이트였다.

### Heap 테스트의 정확한 요청 크기 가정

첫 full-set 실행의 kernel TC는 430 PASS / 3 FAIL이었다. `realloc`의 축소 후 전체 할당량이 예상 48바이트가 아닌 64바이트였고, cleanup 없는 assertion 반환으로 남은 64바이트가 후속 memalign/zalloc의 384 대 320 실패를 만들었다.

현재 allocator는 다음 블록이 사용 중이고 잔여 16바이트가 free-node 헤더 24바이트보다 작으면 원래 64바이트 블록을 유지한다. 이는 `mm_shrinkchunk()`의 정상 동작이다. 커널 전체를 거치지 않고 네트워크 smoke 뒤 heap TC만 실행해도 `mallinfo`에서 48 대 32의 같은 유형의 가정 오류를 재현했다(7 PASS / 1 FAIL).

TC는 각 실제 chunk size를 합산해 mallinfo/heapinfo와 정확히 비교하도록 수정했다. 각 chunk에는 정렬된 최소 요청 크기 이상의 용량과 `잔여 공간 < SIZEOF_MM_FREENODE` 상한을 요구한다. `realloc`의 보존 데이터 검사와 실패 시 cleanup을 추가했고, free 뒤 원래 baseline으로 복귀하는 검사는 유지한다. allocator production 코드는 바꾸지 않았다.

수정 후 네트워크·C++·libc++·파일시스템·SMARTFS·드라이버 뒤에 heap 8개 검사를 각각 수행해 모두 통과했다. 진단용 heap-only 진입 경로는 최종 소스에서 제거했다. 이 결과는 최종 전체 kernel TC 검증과 구분한다.

### 반복 실행 후 남는 자원

다음 full-set 실행에서는 각 TC가 통과해도 종료 후 heap used가 1회차 78,416 → 2회차 79,024바이트로 증가했다. 별도 QEMU에서 GDB로 살아 있는 chunk의 크기와 할당 호출자를 기록해 원인을 분리했다.

- `ifconfig` 호출마다 `lwip_get_ifaddrs()`에서 272바이트가 남았다. `netlib_freeifaddrs()`가 첫 목록 노드만 해제했으며, IPv4의 netmask/destination은 `ifa_addr`의 연속 할당 내부를 가리키는데도 별도 해제를 시도했다. 전체 목록을 순회하고 각 소유 allocation만 한 번 해제한다. private error cleanup에도 같은 소유권과 올바른 순회를 적용했고, 첫 노드를 즉시 root로 연결해 부분 할당 실패도 정리한다.
- `network_tc`마다 connect ITC의 server/client 결과 객체 128~144바이트가 남았다. join 반환 결과를 덮어쓰거나 버리는 대신 공통 helper에서 확인 후 해제한다. 마지막 client 결과 검사와 중복 semaphore 초기화도 바로잡았다.
- filesystem TC마다 `tempnam()` 반환 문자열 두 개가 합계 80바이트 남았다. 두 테스트에서 정상·실패 경로 모두 정리한다.
- filesystem TC마다 `ereport_open()`의 상태 객체 80바이트가 남았다. procfs의 `ereport**`, `ereport/*`가 같은 파일을 두 번 열어 `f_priv`를 덮어썼다. 첫 성공한 open에서 탐색을 종료한다.

주소별 크기 증가 기록은 `memory-probe/`에 있다. 자동 회귀 검사는 수정 전 `ifconfig` 한 번의 +272바이트를 검출해 exit 1로 끝났다(`resource-red/`). 수정 후 `resource-green/`에서 ifconfig/procfs 각 3회와 network TC 3회의 증가가 사라졌고, filesystem은 2회차 76,128바이트, 3회차도 76,128바이트였다. 수정 전 full-set은 모든 기능 TC가 세 번 통과해도 2→3회차 heap +576바이트를 검출해 실패했다(`verified-full-02/`). FIFO 등 첫 실행에 생기는 지속 객체와 반복마다 증가하는 위 누수는 구분한다.

## 최종 결과

자원 정리 수정까지 포함한 최종 clean build는 exit 0으로 완료했다. 같은 ELF로 부팅 한 번, kernel 생략 없이 전체 세 회차를 실행했다. 최종 판정은 `pass_with_known_driver_failures`, container exit 0, wall time 2,272.023초(약 37분 52초)다.

아래 TC 수치는 PASS / FAIL이다.

| 검사 | 1회차 | 2회차 | 3회차 |
|---|---:|---:|---:|
| Network API TC | 163 / 0 | 163 / 0 | 163 / 0 |
| Network peer (loopback·DHCP/ping·양방향 TCP/UDP) | 7 checks PASS | 7 checks PASS | 7 checks PASS |
| C++ smoke | PASS | PASS | PASS |
| libc++ UTC | 795 / 0 | 795 / 0 | 795 / 0 |
| Filesystem TC | 203 / 0 | 203 / 0 | 203 / 0 |
| SmartFS fill/delete | 100회 PASS | 100회 PASS | 100회 PASS |
| Drivers TC | 14 / 8 (기존 미지원) | 14 / 8 (기존 미지원) | 14 / 8 (기존 미지원) |
| Kernel TC | 433 / 0 | 433 / 0 | 433 / 0 |
| 종료 heap used (bytes) | 77,872 | 77,872 | 77,856 |
| 종료 task 수 | 8 | 8 | 8 |
| `/mnt` available blocks (1 KiB) | 1,011 | 1,010 | 1,010 |

두 번째→세 번째 heap used는 16바이트 감소했고, largest free block은 매번 14,321,872바이트였다. 기존 PID 집합 `[0, 1, 2, 3, 4, 5, 7, 8]`이 모든 workload 종료 후 유지됐다. 파일 목록·크기와 `/mnt`, `/tmp`, `/proc` 용량은 두 번째→세 번째가 동일하다. 첫 두 회차의 초기화·저장장치 변화를 제외한 관측 결과이며, 모든 잠재적 누수나 단편화 부재를 증명하는 것은 아니다.

`/mnt` sentinel은 모든 workload와 재마운트 후 유지됐고 마지막에 제거했다. `/mnt/loopfile` 1,024바이트는 driver TC가 남기는 기존 테스트 파일로 회차 간 일정했다. `/tmp`는 비어 있었다. 각 SmartFS 실행에는 별도의 random seek/write/circular-log 검사도 포함된다.


- ELF SHA-256: `c7ee3153b3f6b9c42102e56e5f8c2926d520135e5813c7226171a9901171036a`
- BIN SHA-256: `85a15c4178be59ae5e068d8975fb7fe6d5d70992cf0803a83fb214c6b70b5dea`
- defconfig / `.config` SHA-256: `196d7ada3d78c08a3782ed239786654e372d16d06d03d99fd3dd74306d2db0e1`; byte 단위 동일.
- ELF text/data/bss: 2,443,118 / 1,512 / 2,372,900 bytes. ARMv7-M / Thumb-2 유지.
- 기존 standalone network smoke PASS(32.825초), corrupt echo는 예상 FAIL / exit 1(16.947초).
- 통합 판정기 단위 테스트 11개, Python AST, `git diff --check` 통과.

빌드 로그의 기존 C++ C 전용 옵션 진단 2건과 libcxx-test Makefile의 `Command not found` 10건은 5단계 최종 로그와 종류·횟수가 같다. 이 단계에서 새로 발생한 실패는 아니며, make exit 0과 진단 없는 빌드는 구분한다. 전체 Kconfig parser의 기존 제한도 앞 단계와 동일하다.

## 재현과 증거

```sh
docker run --rm --pull=never --platform linux/arm64 \
  -v "$PWD:/work" -w /work/os tizenrt/tizenrt:2.0.1-arm64-local \
  bash -c 'set -e; make distclean; cd tools; ./configure.sh qemu/build_test; cd ..; make -j4'
python3 tools/qemu-build-test/test-full-set.py -v
python3 tools/qemu-build-test/full-set.py --root . --rounds 3 \
  --output tmp/qemu-build-test-stage06/full-new
```

`full-set.py`는 QEMU user network와 컨테이너 loopback 상대만 사용하며 Docker는 `--network none`이다. 매 workload 전후 atomic `result.json`과 원본 serial log를 남긴다. 실행에는 [0~1단계](QEMU_Build_Test_Stage01.md)의 ARM64 로컬 build/QEMU 이미지를 사용한다.

로컬 증거는 ignored `tmp/qemu-build-test-stage06/`에 보존한다. `diagnostic-01`, `connect-probe`, `connect-red`, `diagnostic-02`, `libcxx-repeat-red`, `verified-full`, `verified-full-02`, `heap-probe-red`, `memory-probe`, `resource-red`는 중간 실패/진단 기록이며 최종 성공 결과와 구분한다. 최종 실행은 `verified-full-03/`, 최종 독립 network smoke와 corrupt negative는 `verified-peer-03/`, `verified-corrupt-03/`다. 소스·실행 이미지·config hash 및 증거 연결은 `manifest.json`에 기록했다. `final-inputs/`에는 실제 이미지·설정·소스 스냅샷을 보존했다.

검증 범위는 QEMU ARMv7-M Flat과 격리된 IPv4 peer다. 물리 보드, SMP/보호 빌드, IPv6 외부 트래픽, 외부 DNS/TLS는 포함하지 않는다. CircleCI 수정·실행과 push도 포함하지 않는다.

## 독립 리뷰

`code-review` 스킬의 Standards/Spec 두 독립 검토는 `3533019f4` 이후 diff와 신규 파일을 대상으로 했다. TCP 목적지 거절, 반복 실행기 판정, 알려진 driver failure 분류, 저장장치 상태 검사, libc++ 카운터 초기화, heap TC의 실제 크기 집계와 cleanup, interface 목록·join 결과·tempnam·procfs의 자원 정리를 검토했다. 소스 검토에서 미해결 필수 표준 위반·actionable finding은 각각 0건이다. 최종 원본 serial, 24개 workload의 PID 집합, 메모리·저장장치, 이미지·소스 hash 및 문서 수치도 두 검토에서 교차 확인했으며 불일치 0건이었다. 검토 결과는 `review-summary.json`에 기록했다.
