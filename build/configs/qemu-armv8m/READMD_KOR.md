# QEMU ARMv8-M MPS2-AN505 사용 가이드

`qemu-armv8m`은 Cortex-M33 기반 QEMU `mps2-an505` 머신에서 TizenRT를
검증하는 ARMv8-M 포트입니다. 기존 LM3S6965 대상과의 역할 구분은
[QEMU Target Roles](../qemu-targets.md)를 참고합니다.

## 지원하는 네 가지 config

지원 범위는 다음 네 가지 config로 고정됩니다.

| Config | 빌드 모델 | 패키지 구성 |
| --- | --- | --- |
| `hello` | TASH와 `kernel_tc`가 포함된 flat 커널 | 커널만 사용 |
| `loadable_all` | protected 커널과 loadable ELF 앱 | 커널, `common`, `app1`, `app2` |
| `loadable_apps` | protected 커널과 loadable ELF 앱 | 커널, `common`, `app1`, `app2` |
| `xip_all` | XIP 커널과 XIP common/app 패키지 | 커널, `common`, `app1` |

loadable 구성에서는 `app1`과 `app2`를 지원하고, `xip_all`은 하나의 XIP
앱을 사용합니다. `xip_all`에서는 `common`을 `app1`보다 먼저 처리해야
하며, 누락 또는 거부된 common 패키지 뒤에 app 시작을 기대하면 안 됩니다.

## 빌드

저장소 루트에서 `os`로 이동한 뒤 하나의 config를 설정합니다. config를
바꿀 때는 먼저 `make distclean`을 실행합니다.

```sh
cd os
make distclean
./tools/configure.sh qemu-armv8m/hello
make
```

`hello` 대신 `loadable_all`, `loadable_apps`, `xip_all`을 지정할 수 있습니다.
커널 산출물은 `build/output/bin/tinyara`이고, loadable 구성은 `app1`,
`xip_all`은 추가로 `common` 산출물을 생성합니다.

## 검증

런타임 검증은 항상 저장소의
`python3 .github/scripts/qemu-armv8m-kernel-tc.py` runner로 실행합니다. 이
runner가 LAN9118 user network, TASH 프롬프트, DHCP, IPv4/IPv6 gateway,
DNS, `network_tc`, `kernel_tc`, serial log와 결과 JSON을 일관되게
관리합니다. 정확한 네 config별 명령과 PASS/FAIL, 거부 마커, 로컬/CI 경계
및 artifact 위치는 영문 README의 **Validate with the QEMU runner**와
**Local and CI validation boundary**를 따릅니다.

로컬 runtime 증거는 ARM64 이미지와 persistent-state runner를 사용한
`hello`, `loadable_all`, `loadable_apps`, `xip_all`에 대해 확보했습니다.
TASH 부팅, binary-manager 패키지 탐색, 앱 로드와 XIP 패키지 배치를
확인했습니다. 2026-07-26 clean build와 새 runner 실행에서 network TC는
네 구성 모두 `PASS : 161, FAIL : 0`, kernel TC는 `hello`가
`PASS : 459, FAIL : 0`, loadable/XIP 세 구성이 각각
`PASS : 447, FAIL : 0`으로 완료되었습니다. 이 결과는 QEMU 소프트웨어
경로의 로컬 증거이며 실제 보드 검증을 대신하지 않습니다.

`hello`는 priority inheritance와 Binary Manager가 모두 꺼져 semaphore
holder tracking을 빌드하지 않습니다. PI-off `loadable_all`과
`loadable_apps`는 Binary Manager holder recovery용으로
`CONFIG_SEM_PREALLOCHOLDERS=16`을 사용합니다. `xip_all`도 priority
inheritance와 Binary Manager용으로 같은 크기의 pool을 사용합니다.
preallocated holder pool은 유한한 holder accounting 자원이므로 동시에
존재할 수 있는 task/semaphore holder pair 수에 맞춰 설정합니다.

loadable/XIP 구성은 runner에 `--state-image`를 지정하면 RAM-backed flash와
A/B boot parameter를 persistent image로 보존할 수 있습니다. 패키지를 다시
빌드한 뒤 새 이미지가 필요하면 기존 state image를 삭제합니다.

후보 commit/push 승인 뒤에는 CI의 positive/negative matrix를 별도로
확인합니다. 로컬 증거와 CI 증거를 섞지 않습니다.

## CI 유지보수와 artifact

CI 계약은 `.github/workflows/qemu-armv8m.yml`에 있습니다. 고정된 runner,
컨테이너 digest, GitHub Actions SHA를 함께 검토하고 변경합니다. positive 결과는
`build/qemu-armv8m/ci-artifacts/<config>/`, negative 결과는
`build/qemu-armv8m/ci-artifacts/negative-<case>/`에 남습니다.

## 참고

- 패키지 배치와 로더 주소의 기준은 runner와 board source입니다. 이 문서에 원시
  QEMU 명령이나 주소를 복사하지 않습니다.
- timer testcase는 남은 시간이 tick 단위인 특성을 반영해 한 tick 오차를 허용합니다.
