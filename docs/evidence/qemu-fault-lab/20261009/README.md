# QEMU fault-lab 관측 증거

2026-10-09의 패치 제거/포함 비교에서 확정한 8개 suite, 36회 실행의 결과를 보존한다.
`matrix`는 자연 중첩 IRQ 3조건 × 3회, `explicit`은 명시적 주입 3조건 × 1회다.
`numbered-nested`와 `numbered-explicit`은 TASH 숫자 선택자 1–6의 각 1회 실행이다.

결과의 `git_head`, 시각, 경로는 최초 실행 당시 값이다. 과거 로그를 통합 브랜치에서
새로 실행한 결과로 바꾸지 않았다. 원래 ELF는 커밋에 포함하지 않으며 SHA-256을
JSON과 manifest에 보존한다. 설정·소스 스냅샷·레지스터 관측·로그는 함께 포함한다.

`pass`는 하네스의 관측 완료를 뜻한다. 중첩 IRQ의 안전한 처리를 뜻하는 것은
`nested-completed`이며, `panic-halt`는 fault 후 정지다. 기대 행렬과 HANG/T32
범위는 [비교 문서](../../../Human/QEMU_ARMv8M_Nested_IRQ_Patch_Comparison.md)를 참고한다.

`consolidated-recheck/`는 통합 checkout의 패치 제거/포함 각각 1–6번, 총 12회
추가 실행이다. `consolidation-validation.json`은 예상/실제 결과와 ELF 해시를 비교한다.
