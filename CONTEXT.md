# TizenRT

TizenRT의 커널 및 시스템 기능에서 사용하는 프로젝트 고유 용어를 정의한다.

## Health Monitoring

**Health Monitor**:
등록한 태스크가 정해진 시간 안에 생존 신호를 갱신해야 한다는 계약과 그 위반을 관찰하는 커널 기능.
_Avoid_: sdog, task_monitor (새 기능의 이름으로 사용할 때)

**Health Contract**:
등록 태스크가 제한시간 안에 생존 신호를 갱신하고 정상 종료 전에 감시를 명시적으로 해제한다는 약속.

**Liveness Checkpoint**:
등록 태스크의 개발자가 의미 있는 진행이 완료됐다고 선언한 코드 지점. CPU 배정이나 context restore는 Liveness Checkpoint가 아니다.

**Health Failure**:
Health Contract가 깨지거나 그 위반을 감시하는 실행 기반이 멈춘 상태. 대기, lock 보유, CPU 독점은 원인 증거이며 그 자체가 Health Failure는 아니다.

**Fatal Diagnostic Phase**:
최초 Health Failure를 확정한 뒤 정상 실행을 중단하고 가능한 장애 정보를 출력하는 단계. 정상 경로의 non-blocking 제약은 적용하지 않는다.

**Minimum Fault Header**:
상세 진단 전에 low-level UART로 출력하는 최대 두 줄의 최소 장애 기록. 장애 종류, offending registration, 시간 상태를 포함한다.

**Health Time**:
Health Monitor가 deadline에 사용하는 64-bit monotonic awake-time. 정상 PM sleep 중에는 Health Contract 시간이 흐르지 않는다.

**Managed Binary Teardown**:
Binary Manager가 update, unload 또는 fault recovery를 위해 loadable binary의 태스크를 의도적으로 제거하는 수명주기 동작. 이 경로의 태스크 소멸은 Health Failure가 아니다.
_Avoid_: Binary Unload (fault recovery까지 함께 뜻할 때)

**Lifetime Monitoring**:
태스크가 시작한 뒤 cooperative exit 직전까지 하나의 Health Contract를 유지하는 감시 범위. 정상 idle 상태도 주기적으로 Liveness Checkpoint를 통과해야 한다.

**Scoped Monitoring**:
메시지 처리나 callback 실행처럼 경계가 있는 작업 동안만 Health Contract를 유지하는 감시 범위. 감시 범위 밖에서 태스크가 사라지는 것은 검출하지 않는다.

**Busy Batch**:
workqueue에서 첫 ready callback을 실행하기 직전부터 연속된 ready callback을 모두 처리하고 idle wait로 들어가기 직전까지의 Scoped Monitoring 구간. callback 완료는 Liveness Checkpoint이며 idle 상태는 감시하지 않는다.

**Lazy Deadline Heap**:
Health Monitor가 가장 가까운 Health Contract deadline을 찾기 위해 유지하는 index. Liveness Checkpoint마다 정렬하지 않고 만료 후보를 확인할 때 연장된 deadline을 반영한다.
