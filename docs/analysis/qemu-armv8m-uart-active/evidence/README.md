# 재현 증거 파일

각 실행 폴더의 `result.json`에는 QEMU 버전, ELF SHA-256, 실행 인자, breakpoint 시점의 CPU/NVIC/UART 상태와 판정이 들어 있다. `hello-p40` 및 `nested-p40`에는 전체 serial, QEMU exception, GDB-RSP 로그도 보관했다. 대조군은 비교에 필요한 `result.json`을 보관했다.

개인 로컬 경로는 결과 JSON에서 `$WORKTREE`로 치환했고 임시 Unix socket 이름은 `<TEMP>`로 바꿨다. serial 로그의 빌드 사용자 표시도 `[redacted]`로 가렸다. 이 정리는 실행 상태 값이나 이벤트 순서를 변경하지 않는다.
