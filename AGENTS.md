# TizenRT AI 개발 문서

이 저장소의 Mac mini Apple Silicon 및 `tizenrt-2.0.1-arm64` 개발 기준은 아래 문서를 먼저 참고한다.

- [Mac QEMU ARMv8-M 빌드, TASH, `kernel_tc`](docs/AI/Mac_QEMU_ARMv8M_TASH_KernelTC.md)
- [AI Build Runbook](docs/AI/AI_Build_Runbook.md)
- [대표 보드와 빌드 레시피](docs/AI/Representative_Boards.md)
- [`bk7239n/hello`와 `qemu-armv8m/hello` defconfig 비교](docs/AI/BK7239N_QEMU_ARMv8M_Hello_Defconfig_Comparison.md)
- [사람용 QEMU 터미널 실행 가이드](docs/Human/QEMU_ARMv8M_Terminal_Guide.md)

## AI 작업 기본 규칙

- 명령은 저장소 루트 또는 문서가 지정한 `os/` 디렉터리에서 실행한다.
- macOS 기본 Bash 3.2 대신 Homebrew Bash 4 이상을 사용한다.
- 빌드·설정은 `./dbuild.sh menu`를 기준으로 한다. 현재 설정의 출력물만 지우려면 `4. Clean Build`, 보드/config를 바꾸며 `distclean`하려면 `5. Clean Build and Re-Configure`를 선택한다.
- `dbuild.sh`가 Docker architecture를 감지해 `--platform`을 자동 전달하므로 `DOCKER_DEFAULT_PLATFORM`을 지정하지 않는다. 직접 `docker buildx` 또는 `docker run`을 실행하는 경우에만 명시적인 platform을 사용한다.
- QEMU 통과 결과는 하드웨어 보드 동작의 증거로 간주하지 않는다.
- 빌드 결과와 Git 상태를 함께 확인하고, `._*` AppleDouble 파일은 소스 변경으로 오인하지 않는다.
