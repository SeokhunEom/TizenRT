# 6단계: HW watchdog 연결

전제: 5단계가 확인·커밋됐고 사용자가 6단계 시작을 지시했다. [공통 진행 방식](README.md)과 [설계 문서](../HealthMonitorImplementationPlan.md)의 8.2절을 사용한다.

## 목표

실제 HW WDT 시작·keepalive와 health monitor 실행을 연결하고, 정상 절전과 장애 리셋을 확인한다. 이 단계에서 보드 의존적인 연결 사항을 해결한다.

## 구현 범위

1. 보드 초기화, HW WDT 시작, 기존 장치 드라이버 초기화의 순서를 확인하고 필요한 최소 연결을 구현한다.
2. `WATCHDOG_FOR_IRQ`를 사용하는 구성과 timeout 값을 검증용 설정에 반영한다. 기존 초기화가 이를 덮어쓰는지 확인한다.
3. 정상 tick, 검사 연기, 만료 확정 각각에서 keepalive를 수행하는 조건을 구체화하고 연결한다. 별도의 100ms SW 장애 판정은 만들지 않는다.
4. 정상 sleep 중 WDT 카운트·갱신 동작을 확인하고 기존 PM wakeup과 필요한 연동을 구현한다.
5. SW timeout 후 기존 PANIC이 완료되거나 진행되지 못하는 경우의 HW 리셋 경로를 확인한다.

예상 변경 위치: amebasmart watchdog·보드 초기화, tick keepalive 연결, 필요한 PM 보완. 기준 defconfig의 최종 제품 활성화는 통합 검증과 함께 7단계에 둔다.

하드웨어 제약 때문에 합의한 동작을 바꿔야 한다면 근거와 가장 작은 변경안을 사용자에게 제시한다. 앱이나 다른 HW WDT 사용자가 있는지도 확인해 변경 영향을 설명한다.

## 이 단계 완료 시 동작

실제로 시작된 HW WDT가 의도한 keepalive 경로와 연결된다. CPU0 tick 중단과 정상 sleep을 구분해 검증하며, CPU1만의 IRQ 마스킹까지 독립적으로 감시한다고 확장하지 않는다.

## 검증

- 설정 활성화뿐 아니라 실제 WDT 시작과 적용 timeout을 확인한다.
- CPU0 tick/keepalive 중단 시 HW 리셋과 reboot reason을 확인한다.
- 정상 tick과 정상 sleep에서 불필요한 HW 리셋이 발생하지 않는지 확인한다.
- 검사가 잠금 경합으로 연기되는 경우의 keepalive 동작을 확인한다.
- SW timeout의 신규 reason, PANIC, HW fallback 결과를 구분해 기록한다.

## 사용자 확인 항목

- WDT는 누가 언제 시작하고 갱신하며, 어떤 조건에서 갱신을 멈추는가?
- 선택한 timeout과 sleep 연동이 실제 시스템 사용 방식에 맞는가?
- 기존 watchdog 사용 방식이나 보드 초기화에 예상하지 못한 변경이 있는가?

완료 기준: 위 질문에 실제 호출 경로와 검증 결과로 답할 수 있다. 보드 검증이 미실행이면 HW 리셋 동작 확인 완료로 표시하지 않는다. 사용자 확인 후 커밋한다.
