# QEMU build_test: 5단계 네트워크

검증일: 2026-09-18 KST. 브랜치: `codex/qemu-build-test`.
기준은 4단계 커밋 `c00963eb14307fc855efc818eb0d7f1b67fd39f5`다.

## 구성과 진행 순서

기존 ARMv7-M / Cortex-M3 Flat, C++11/libc++, RAMMTD/SmartFS 구성을 유지했다. 네트워크 스택을 먼저 켜 빌드·부팅하고, DHCP/ping, 기존 API TC, TCP/UDP payload 검증 순서로 확대했다.

- lwIP IPv4/IPv6, TCP/UDP, ICMP, ARP, IGMP, Ethernet, loopback, socket 옵션, DHCP client와 TASH 네트워크 명령을 활성화했다.
- 기존 `qemu/tc_16m` 설정을 참고하되 Wi-Fi manager/가상 WLAN 및 DHCP server는 추가하지 않았다. 인터페이스는 `lo`, `eth0`다.
- IPv6 소켓 TC의 의존성을 충족하려고 IPv6를 활성화했다. Flat 빌드의 기존 `netif_gen_stable_private_id()`가 `mbedtls_sha256()`을 사용하므로 `NET_SECURITY_TLS`도 필요했다. 이는 실제 TLS 연결이나 인증서 검증을 통과했다는 뜻이 아니다.
- 기존 네트워크 TC의 정의된 개별 옵션을 켰다. `TC_NET_ALL`은 정의/소스가 없는 `TC_NET_SELECT`까지 select하므로 사용하지 않았다. `tc_net_dhcpc.c` 자체는 no-op이어서 실제 DHCP는 별도 smoke로 확인한다.
- `TC_NET_PEER`는 loopback 또는 외부 상대와 binary payload를 비교하는 `network_peer` 명령이다. 기존 `network_tc` 집계와 별도로 실행한다. 일반 TASH 및 built-in 앱 등록 경로를 모두 연결했다.

## 빌드와 드라이버 포팅

첫 네트워크 빌드는 `up_netinitialize` undefined reference로 실패했다. Tiva Make.defs가 LM3S Ethernet 소스를 포함하지 않았다. 소스 목록을 복구하자 삭제된 `struct netif.d_buf`와 이전 `ethernetif_input()` 호출 규약 때문에 컴파일에 실패했다.

`lm3s_ethernet.c`를 현재 network manager에 연결했다.

- `NM_ETHERNET` / `netdev_register`와 Ethernet init/enable/disable callback을 사용한다.
- 송신은 network manager가 제공한 frame을 FIFO로 복사한다. 수신은 HPWORK에서 FIFO를 비우고 `netdev_input()`으로 전달한다. 수신 버퍼는 송신 버퍼와 분리한다.
- FIFO length의 2바이트 길이 필드와 4바이트 FCS를 제외해 정확한 frame을 전달한다. 길이를 검사하고, 마지막 partial word도 byte 단위로 처리한다.
- lwIP는 `netif.mtu`를 IP MTU로 사용하므로 Ethernet 헤더를 더한 공간이 필요하다. 기존 netmgr는 `mtu + 12`만 요청했다. `mtu + ETH_HDRLEN`으로 바꾸고 pbuf 복사 전에 같은 상한을 검사한다. 이 구성은 IP MTU 1512, Ethernet frame 최대 1526바이트다. IPv4 UDP payload 1484바이트가 이 경계를 사용한다.
- RX를 마스크한 동안의 이벤트를 TX ISR이 지우지 않도록 enabled interrupt만 acknowledge한다. `work_queue`의 실제 중복 대기 반환값인 `-EALREADY`도 처리한다.

송신 용량과 IRQ 경로는 독립 리뷰에서 확인했다. 할당 정렬 여유 때문에 원래 코드의 인접 heap 손상이 관측됐다고 주장하지 않는다. IRQ 경로도 실제 target 실패 재현과 구분한다. QEMU v2.12 소스에서 IACK가 RIS를 지우고 IM 변경이 FIFO에서 이벤트를 재생성하지 않는 것을 확인한 코드·interleaving 분석이다. [QEMU stellaris_enet.c v2.12.0](https://github.com/qemu/qemu/blob/v2.12.0/hw/net/stellaris_enet.c)

## 네트워크 검증 방식

`tools/qemu-build-test/network-smoke.py`가 격리된 Docker 컨테이너 안에서 QEMU와 Python 상대 프로세스를 실행한다. Docker는 `--network none`이며, QEMU user network와 컨테이너 loopback 사이에서만 통신한다. macOS 네트워크 설정 변경이나 외부 서비스 의존성은 없다.

- loopback TCP/UDP: guest server와 client가 실제 socket으로 왕복하고 양쪽 완료를 확인한다.
- DHCP: QEMU user network에서 `eth0`가 `10.0.2.15`를 받는다.
- ping: `10.0.2.2`에 3회 송신/3회 수신을 종료 집계로 확인한다. 비동기 TASH prompt는 완료로 취급하지 않는다.
- guest client → host server 및 host client → guest server: TCP/UDP 각각 검증한다. Python과 guest가 각각 데이터 내용을 검사한다.
- payload는 1, 3, 64, 513, 1024, 1484바이트의 결정적 binary 패턴이다. 한 교환의 각 방향에서 3089바이트를 확인한다. TCP는 partial I/O를 처리하고 UDP는 길이·내용·상대를 확인한다.
- `ifdown eth0` → `ifup eth0` 후 DHCP/ping/양방향 TCP/UDP를 반복한다. 실제 출력인 `<DOWN,RUNNING>`을 eth0 행에서 판정한다. `lo`의 UP 상태와 혼동하지 않는다.
- `/mnt` sentinel 보존과 종료 후 task/메모리/interface 상태를 기록한다.
- `--corrupt-reply`는 host echo의 첫 byte를 바꾼다. 정상 검증의 반대 결과인 FAIL과 nonzero exit가 나와야 한다.

## 최종 검증

최종 clean build는 exit 0으로 완료했다. 아래 검사는 같은 ELF를 각각 새 QEMU에서 실행했다. 실제 `.config`와 defconfig는 byte 단위로 동일하다.

| 검사 | 결과 |
|---|---|
| Clean build | PASS (exit 0) |
| Network TC | PASS 161 / FAIL 0, 0.350초 |
| Network smoke | 12개 PASS, 32.580초, storage guard 보존 |
| Corrupt echo negative check | 예상대로 FAIL / exit 1, errno=EIO, 16.910초 |
| C++ smoke / libc++ UTC | smoke PASS 32.337초 / UTC 795 PASS·0 FAIL 26.979초 |
| Kernel TC | PASS 433 / FAIL 0, 513.572초, storage guard 보존 |
| Filesystem / SmartFS | filesystem 203 PASS / 0 FAIL, 72.906초 / SmartFS 100회 PASS, 77.715초 (회당 최소 213개 파일) |
| Drivers | PASS 14 / FAIL 8, 10.731초; 4단계 failure lines와 동일 |

- ELF SHA-256: `f19af701fdd453e6cfdc9b80ca653421f08d884fda30ddc494a4349a39d8cbe9`
- BIN SHA-256: `9dd0a82ba2d737b42385464c90d596ebdefb3f526b19eba743b96a60ea24fa2f`
- defconfig / `.config` SHA-256: `196d7ada3d78c08a3782ed239786654e372d16d06d03d99fd3dd74306d2db0e1`
- ELF text/data/bss: 2,441,946 / 1,512 / 2,372,900 bytes. BIN: 2,443,460 bytes. ARMv7-M / Thumb-2 attributes 유지.
- 기존 4단계에도 있던 C++ 컴파일러의 C 전용 옵션 진단 2건(`-Wstrict-prototypes`, `-Wno-implicit-function-declaration`)은 최종 build log에도 있다. 이 단계에서 제거하지 않았으며, make exit 0과 진단 없는 빌드는 구분한다.
- 전체 Kconfig 정규화의 기존 parser 제한은 [3단계](QEMU_Build_Test_Stage03.md)와 같다. 실제 configure.sh / clean build와 설정 동일성을 검증했다.

초기 단계의 기록은 최종 이미지 증거와 구분한다. IPv4-only 부팅/DHCP/ping 성공, network TC 161 PASS / 0 FAIL, 전체 peer smoke 12개 PASS를 확인했다. 처음 peer 실행은 built-in 명령 등록 누락을, 다음 실행은 `<DOWN>` 출력 기대값 오류를 검출했다. 이를 수정한 로그도 보존한다.

## 재현과 증거

빌드와 QEMU 이미지는 [0~1단계](QEMU_Build_Test_Stage01.md)의 ARM64 로컬 이미지다.

```sh
docker run --rm --pull=never --platform linux/arm64 \
  -v "$PWD:/work" -w /work/os tizenrt/tizenrt:2.0.1-arm64-local \
  bash -c 'set -e; make distclean; cd tools; ./configure.sh qemu/build_test; cd ..; make -j4'
python3 tools/qemu-build-test/run-testcases.py --root . \
  --suite network_tc --check-storage --timeout 900 \
  --output tmp/qemu-build-test-stage05/network-new
python3 tools/qemu-build-test/network-smoke.py --root . \
  --output tmp/qemu-build-test-stage05/peer-new
# Expected result: FAIL, nonzero exit, NETPEER FAIL in serial.log.
python3 tools/qemu-build-test/network-smoke.py --root . --corrupt-reply \
  --output tmp/qemu-build-test-stage05/corrupt-new
```

로컬 원본 증거는 ignored `tmp/qemu-build-test-stage05/`에 보존한다. 최종 ELF/BIN/config, source snapshot, SHA-256, build log, runtime result.json/serial.log를 함께 기록한다. `verified-*`가 최종 runtime 증거이고 `manifest.json`이 전체 결과를 연결한다. `git diff --check`와 Python AST 검사도 통과했다. 중간 실패 로그도 보존한다.

## 리뷰와 범위

`code-review` 스킬의 Standards/Spec 두 독립 검토는 `c00963eb1` 이후 작업 diff와 새 파일을 대상으로 했다. 기준 요구사항은 사용자 대화의 5단계 범위이며, 별도 issue tracker는 사용하지 않았다. 최종 소스 검토에서 Standards/Spec 미해결 지적은 각각 0건이었다. 리뷰와 최종 runtime 결과는 별도 증거다.

검증 범위는 QEMU ARMv7-M Flat 및 격리된 IPv4 peer다. IPv6 소켓/API·loopback interface는 활성화했지만 IPv6 외부 송수신, 외부 DNS, TLS handshake, 물리 Ethernet, SMP/보호 빌드는 별도 검증 대상이다. QEMU는 TX를 동기 처리하므로 실제 MAC의 장시간 busy/timeout 동작을 이 결과와 동일시하지 않는다.

CircleCI 수정·실행, 원격 push 및 다음 통합 반복 단계는 이번 작업에 포함하지 않았다. 기존 PWM/watchdog/ADC 장치 미지원 실패는 별도로 집계한다.
