# 로컬 QEMU 테스트 가이드

이 문서는 로컬 PC에서 Docker와 QEMU를 이용해 TizenRT `qemu-virt`, `qemu-mps2-an505` 구성을 빌드하고 runtime 테스트까지 수행하는 방법을 정리한다.

## 1. 목적

- 로컬에서 GitHub Actions와 유사한 흐름으로 `configure -> build -> download -> runtime test`를 재현한다.
- 지원 대상은 다음 5개 구성이다.
  - `qemu-virt/flat`
  - `qemu-virt/loadable_xip_elf`
  - `qemu-mps2-an505/flat`
  - `qemu-mps2-an505/loadable_elf`
  - `qemu-mps2-an505/loadable_xip_elf`

## 2. 준비 사항

- Docker Desktop이 실행 중이어야 한다.
- 작업 디렉터리는 저장소 루트여야 한다.
- 기본 빌드 이미지는 `tizenrt/tizenrt:2.0.1`를 사용한다.
- 기본 런타임 이미지는 [tools/qemu_test/Dockerfile](/C:/Users/seokhun/Desktop/TizenRT/tools/qemu_test/Dockerfile)에서 직접 빌드한다.

## 3. 런타임 이미지 준비

저장소 루트에서 한 번만 빌드하면 된다.

```powershell
cd C:\Users\seokhun\Desktop\TizenRT

docker build -t tizenrt-qemu-test:ci -f tools/qemu_test/Dockerfile .
```

## 4. 기본 실행 순서

로컬 테스트는 항상 아래 순서로 진행한다.

1. `configure`
2. `build`
3. `download`
4. `run_qemu_tests.py`

`download`를 생략하면 runtime 이미지가 staging되지 않아서 테스트가 실패한다.

## 5. 예제: qemu-mps2-an505/loadable_elf

### 5.1 build/download

```powershell
cd C:\Users\seokhun\Desktop\TizenRT

docker run --rm `
  -v "${PWD}:/root/tizenrt" `
  -w /root/tizenrt/os `
  tizenrt/tizenrt:2.0.1 `
  bash -lc "./tools/configure.sh qemu-mps2-an505/loadable_elf && make -j1 && make download"
```

### 5.2 runtime test

```powershell
docker run --rm --network none `
  -v "${PWD}:/workspace" `
  -w /workspace `
  tizenrt-qemu-test:ci `
  python3 tools/qemu_test/run_qemu_tests.py `
    --results-dir /workspace/test-results/local-qemu-mps2-loadable-elf `
    --board qemu-mps2-an505 `
    --boot-timeout 120 `
    --command-timeout 10 `
    --tc-timeout 60
```

## 6. 다른 구성 실행 방법

구성별로 바뀌는 부분은 `configure.sh` 인자와 `--board` 값뿐이다.

### 6.1 qemu-mps2-an505

`flat`

```powershell
docker run --rm `
  -v "${PWD}:/root/tizenrt" `
  -w /root/tizenrt/os `
  tizenrt/tizenrt:2.0.1 `
  bash -lc "./tools/configure.sh qemu-mps2-an505/flat && make -j1 && make download"

docker run --rm --network none `
  -v "${PWD}:/workspace" `
  -w /workspace `
  tizenrt-qemu-test:ci `
  python3 tools/qemu_test/run_qemu_tests.py `
    --results-dir /workspace/test-results/local-qemu-mps2-flat `
    --board qemu-mps2-an505 `
    --boot-timeout 120 `
    --command-timeout 10 `
    --tc-timeout 60
```

`loadable_elf`

```powershell
docker run --rm `
  -v "${PWD}:/root/tizenrt" `
  -w /root/tizenrt/os `
  tizenrt/tizenrt:2.0.1 `
  bash -lc "./tools/configure.sh qemu-mps2-an505/loadable_elf && make -j1 && make download"

docker run --rm --network none `
  -v "${PWD}:/workspace" `
  -w /workspace `
  tizenrt-qemu-test:ci `
  python3 tools/qemu_test/run_qemu_tests.py `
    --results-dir /workspace/test-results/local-qemu-mps2-loadable-elf `
    --board qemu-mps2-an505 `
    --boot-timeout 120 `
    --command-timeout 10 `
    --tc-timeout 60
```

`loadable_xip_elf`

```powershell
docker run --rm `
  -v "${PWD}:/root/tizenrt" `
  -w /root/tizenrt/os `
  tizenrt/tizenrt:2.0.1 `
  bash -lc "./tools/configure.sh qemu-mps2-an505/loadable_xip_elf && make -j1 && make download"

docker run --rm --network none `
  -v "${PWD}:/workspace" `
  -w /workspace `
  tizenrt-qemu-test:ci `
  python3 tools/qemu_test/run_qemu_tests.py `
    --results-dir /workspace/test-results/local-qemu-mps2-loadable-xip `
    --board qemu-mps2-an505 `
    --boot-timeout 120 `
    --command-timeout 10 `
    --tc-timeout 60
```

### 6.2 qemu-virt

`flat`

```powershell
docker run --rm `
  -v "${PWD}:/root/tizenrt" `
  -w /root/tizenrt/os `
  tizenrt/tizenrt:2.0.1 `
  bash -lc "./tools/configure.sh qemu-virt/flat && make -j1 && make download"

docker run --rm --network none `
  -v "${PWD}:/workspace" `
  -w /workspace `
  tizenrt-qemu-test:ci `
  python3 tools/qemu_test/run_qemu_tests.py `
    --results-dir /workspace/test-results/local-qemu-virt-flat `
    --board qemu-virt `
    --boot-timeout 120 `
    --command-timeout 10 `
    --tc-timeout 60
```

`loadable_xip_elf`

```powershell
docker run --rm `
  -v "${PWD}:/root/tizenrt" `
  -w /root/tizenrt/os `
  tizenrt/tizenrt:2.0.1 `
  bash -lc "./tools/configure.sh qemu-virt/loadable_xip_elf && make -j1 && make download"

docker run --rm --network none `
  -v "${PWD}:/workspace" `
  -w /workspace `
  tizenrt-qemu-test:ci `
  python3 tools/qemu_test/run_qemu_tests.py `
    --results-dir /workspace/test-results/local-qemu-virt-loadable-xip `
    --board qemu-virt `
    --boot-timeout 120 `
    --command-timeout 10 `
    --tc-timeout 60
```

## 7. 수동으로 QEMU만 실행하는 방법

`make download` 이후에는 저장소 루트에 [run_qemu.sh](/C:/Users/seokhun/Desktop/TizenRT/run_qemu.sh)가 생성된다.

```powershell
docker run --rm --network none `
  -v "${PWD}:/workspace" `
  -w /workspace `
  tizenrt-qemu-test:ci `
  bash /workspace/run_qemu.sh
```

이 방식은 부팅과 CLI 동작을 수동으로 보고 싶을 때 사용한다. 자동 판정은 하지 않는다.

## 8. 결과 확인 위치

`run_qemu_tests.py`는 `--results-dir` 아래에 결과를 남긴다.

- `summary.md`
- `qemu-transcript.log`
- `qemu-tests.xml`
- `runtime-metadata.json`

예를 들어 `qemu-mps2-an505/loadable_elf` 예제 결과는 아래 경로에 남는다.

- [test-results](/C:/Users/seokhun/Desktop/TizenRT/test-results)

## 9. 권장 사항

- 구성을 바꿔가며 연속 빌드할 때는 항상 다시 `configure`부터 수행한다.
- 로컬 재현성과 디버깅을 위해 `make -j1`을 권장한다.
- Docker Desktop이 내려가 있으면 `docker run` 자체가 실패한다.
- runtime test는 반드시 `make download` 이후에 실행한다.

## 10. 자주 보는 실패 원인

- `run_qemu.sh` 또는 staged runtime image가 없으면 `download` 단계가 정상 수행되지 않은 것이다.
- `Lockup` 또는 `HardFault`가 보이면 잘못된 runtime image가 attach되었거나 QEMU command가 구성과 맞지 않는 경우가 많다.
- `drivers_tc` timeout은 실제 hang일 수도 있지만, transcript 노이즈 때문에 testcase summary를 하네스가 놓치는 경우도 있다. 최신 [run_qemu_tests.py](/C:/Users/seokhun/Desktop/TizenRT/tools/qemu_test/run_qemu_tests.py) 기준으로 재확인한다.
