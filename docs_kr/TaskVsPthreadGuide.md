# Task와 Pthread 차이 가이드

이 문서는 TizenRT에서 `task_create()`로 만드는 task와 `pthread_create()`로 만드는 pthread의 차이를 정리한다. 둘 다 같은 scheduler가 관리하는 실행 단위이고 PID를 갖지만, 생성 모델, task group 소속, 인자 전달, 종료 회수 방식이 다르다.

## 한눈에 보는 차이

| 구분 | task | pthread |
|---|---|---|
| 대표 API | `task_create()`, `task_delete()`, `task_restart()` | `pthread_create()`, `pthread_join()`, `pthread_detach()`, `pthread_cancel()`, `pthread_exit()` |
| 헤더 | `#include <sched.h>` | `#include <pthread.h>` |
| 내부 TCB 타입 | `struct task_tcb_s` | `struct pthread_tcb_s` |
| TCB 플래그 | `TCB_FLAG_TTYPE_TASK` | `TCB_FLAG_TTYPE_PTHREAD` |
| ID 타입 | `pid_t` | `pthread_t`, TizenRT에서는 `pid_t` typedef |
| 생성 결과 | 새 task group을 갖는 독립 실행 단위 | 부모와 같은 task group에 붙는 thread |
| entry 함수 | `int main(int argc, char *argv[])` 형태 | `void *(*start)(void *)` 형태 |
| 인자 전달 | `argv[]` 문자열 배열 | `void *arg` 하나 |
| 종료 값 회수 | task 전용 join API 없음 | `pthread_join()`으로 `void *` 반환값 회수 |
| 자동 회수 | 종료되면 task 종료 경로에서 TCB/stack 회수 | joinable thread는 join/detach 관리가 필요 |
| POSIX 호환성 | TizenRT/NuttX 계열 non-standard API | POSIX pthread API에 가까운 모델 |

## 공통점

task와 pthread는 완전히 별개의 실행 엔진이 아니다.

- 둘 다 scheduler의 ready-to-run/pending list에서 관리된다.
- 둘 다 PID를 갖고, TizenRT에서는 `pthread_t`도 `pid_t`로 정의된다.
- 둘 다 stack과 TCB를 할당받고 `task_activate()`를 통해 실행 가능 상태가 된다.
- 둘 다 priority와 scheduling policy의 영향을 받는다.
- 둘 다 `CONFIG_MAX_TASKS` 한도에 포함된다. `task_create()`와 `pthread_create()` 모두 `g_alive_taskcount == CONFIG_MAX_TASKS`이면 생성을 거부한다.
- 둘 다 종료 시 공통 task 종료 경로를 지나 TCB와 stack을 해제한다.

핵심 차이는 "실행 단위가 무엇을 소유하느냐"다. task는 독립 실행 단위로 새 task group을 만들고, pthread는 현재 task group 안에서 함께 동작하는 worker thread로 만들어진다.

## 생성

### task 생성

```c
#include <sched.h>

static int worker_main(int argc, char *argv[])
{
	/* ... */
	return 0;
}

pid_t pid = task_create("worker", 100, 4096, worker_main, NULL);
if (pid < 0) {
	/* errno 확인 */
}
```

`task_create()`는 다음 순서로 새 task를 만든다.

1. `struct task_tcb_s`를 할당한다.
2. 새 task group을 할당한다.
3. file/socket descriptor 등 task group 리소스를 설정한다.
4. stack을 만든다.
5. `task_schedsetup()`으로 `TCB_FLAG_TTYPE_TASK`를 설정한다.
6. `task_argsetup()`으로 task 이름과 `argv[]`를 저장한다.
7. task group을 초기화하고 `task_activate()`로 실행한다.

task entry는 `main()`과 같은 `int (int argc, char *argv[])` 형태다. `task_start()`가 `argv[]` 개수를 계산해서 entry를 호출하고, entry가 return하면 `exit(exitcode)`를 호출한다.

### pthread 생성

```c
#include <pthread.h>

static void *worker_thread(void *arg)
{
	/* ... */
	return arg;
}

pthread_t tid;
int ret = pthread_create(&tid, NULL, worker_thread, NULL);
if (ret != 0) {
	/* ret 자체가 errno 값 */
}
```

`pthread_create()`는 다음 순서로 새 pthread를 만든다.

1. `struct pthread_tcb_s`를 할당한다.
2. 새 task group을 만들지 않고 부모 task group에 bind/join한다.
3. join/detach 관리를 위한 `struct join_s`를 할당한다.
4. stack을 만든다.
5. attribute에서 priority, scheduler policy, stack size 등을 결정한다.
6. `pthread_schedsetup()`으로 `TCB_FLAG_TTYPE_PTHREAD`를 설정한다.
7. `pthread_argsetup()`으로 `void *arg` 하나를 저장한다.
8. `task_activate()`로 실행한 뒤, 새 pthread가 join 정보를 등록할 때까지 기다린다.

pthread entry는 `void *(*)(void *)` 형태다. `pthread_start()`가 entry를 호출하고, entry가 return하면 반환값을 `pthread_exit()`로 넘긴다.

## 사용 모델

### task가 적합한 경우

task는 별도 실행 단위로 다루고 싶은 작업에 적합하다.

- shell 명령처럼 독립적으로 실행되는 application entry
- testcase dispatcher처럼 개별 실행 단위를 분리해야 하는 경우
- task manager가 독립 task로 감시하거나 제어해야 하는 경우
- 별도 task group, child status, file descriptor 그룹 처리가 의미 있는 경우
- `task_restart()`처럼 task 단위 재시작 모델이 필요한 경우

예를 들어 TASH는 명령 실행이나 shell 본체 실행에 `task_create()`를 사용한다.

### pthread가 적합한 경우

pthread는 같은 task group 안에서 협업하는 worker에 적합하다.

- daemon 내부 monitor thread
- network client/server 동시 실행
- 한 application 내부의 비동기 worker
- 같은 주소 공간과 리소스를 공유하면서 `pthread_join()`으로 완료를 기다려야 하는 경우
- mutex, condition variable, thread-specific data, cleanup handler 등 POSIX pthread 기능이 필요한 경우

예를 들어 CPU load monitor, stack monitor, network testcase는 `pthread_create()`로 같은 application 안의 worker thread를 만든다.

## 종료와 삭제

### task 종료

task는 다음 방식으로 종료된다.

- entry 함수가 return하면 `task_start()`가 `exit(exitcode)`를 호출한다.
- 자기 자신을 끝내려면 `exit()` 또는 `task_delete(0)`를 사용할 수 있다.
- 다른 task가 끝내려면 `task_delete(pid)`를 호출한다.
- `task_restart(pid)`는 기존 task를 종료한 뒤 같은 조건으로 다시 초기화한다.

`task_delete()`는 pthread용 API가 아니다. 내부에서 대상 TCB가 `TCB_FLAG_TTYPE_PTHREAD`가 아니어야 한다고 검증한다. pthread를 task처럼 지우려 하지 말고 `pthread_cancel()`과 `pthread_join()` 또는 `pthread_detach()` 모델을 사용해야 한다.

### pthread 종료

pthread는 다음 방식으로 종료된다.

- start routine이 return하면 `pthread_start()`가 `pthread_exit(return_value)`를 호출한다.
- pthread가 직접 `pthread_exit(value)`를 호출할 수 있다.
- 다른 thread가 `pthread_cancel(thread)`로 cancel을 요청할 수 있다.
- joinable pthread는 `pthread_join(thread, &value)`로 종료를 기다리고 반환값을 받을 수 있다.
- 반환값이 필요 없으면 `pthread_detach(thread)`로 detach해야 한다.

`pthread_exit()`는 cleanup handler, pthread key destructor, robust mutex 복구, join 완료 처리를 수행한 뒤 `_exit()`로 공통 종료 경로에 들어간다.

주의할 점은 joinable pthread를 만들고 아무도 `pthread_join()`이나 `pthread_detach()`를 하지 않으면 join 정보가 남을 수 있다는 점이다. 실제 구현의 `pthread_destroyjoin()` 설명에도 detach도 join도 없으면 join info가 해제되지 않을 수 있다고 되어 있다.

## 동기화와 반환값

task에는 pthread 같은 join 모델이 없다. task의 완료를 기다려야 한다면 semaphore, message queue, signal, event, watchdog/task monitor 등 별도 IPC나 동기화 수단을 설계해야 한다.

pthread에는 join/detach 모델이 내장되어 있다.

- `pthread_join()`은 같은 task group에 속한 pthread의 종료를 기다린다.
- `pthread_join()`은 thread 자신을 join하려 하면 `EDEADLK`를 반환한다.
- 대상이 살아 있지만 join 정보가 없으면 task이거나 detach된 thread로 보고 `EINVAL` 또는 `ESRCH`를 반환한다.
- `pthread_tryjoin_np()`는 non-blocking join이며, 아직 살아 있으면 `EBUSY`를 반환한다.

반환값이 필요한 worker는 pthread로 만드는 편이 단순하다. 독립 task에서 결과가 필요하면 message queue나 shared state 보호 구조를 따로 둔다.

## 리소스와 task group

task와 pthread를 나누는 가장 중요한 내부 기준은 task group이다.

- task는 `group_allocate()`와 `group_initialize()`를 통해 새 task group을 만든다.
- pthread는 `group_bind()`와 `group_join()`을 통해 부모 task group에 붙는다.
- task의 parent/child 관계와 child status 저장은 task에만 적용되고, pthread에는 적용하지 않는다.
- pthread join 정보는 task group의 join list에 저장된다.

따라서 같은 application 내부에서 파일 descriptor, address environment, binary manager context, join semaphore 같은 그룹 리소스를 공유해야 하면 pthread가 자연스럽다. 반대로 독립 실행 단위로 lifecycle을 분리해야 하면 task가 자연스럽다.

## 인자와 이름

task는 `name`과 `argv[]`를 갖는다.

```c
char *argv[] = { "worker", "arg1", "arg2", NULL };
pid_t pid = task_create("worker", 100, 4096, worker_main, argv);
```

pthread는 기본 이름이 `"<pthread>"`이고, 인자는 `void *arg` 하나다.

```c
struct context ctx;
pthread_t tid;
pthread_create(&tid, NULL, worker_thread, &ctx);
```

pthread 이름이 필요하면 `pthread_setname_np(thread, name)`을 사용한다. TizenRT에서는 이 매크로가 `prctl(PR_SET_NAME_BYPID, ...)`로 연결된다.

## scheduling과 priority

task는 생성 인자로 priority와 stack size를 직접 받는다.

```c
task_create("name", priority, stack_size, entry, argv);
```

pthread는 `pthread_attr_t`로 priority, policy, stack size, inheritsched, affinity 등을 지정한다. attr이 `NULL`이면 `g_default_pthread_attr`가 사용된다. 기본 pthread priority는 `PTHREAD_DEFAULT_PRIORITY`, 기본 stack size는 `PTHREAD_STACK_DEFAULT`다.

```c
pthread_attr_t attr;
struct sched_param param;

pthread_attr_init(&attr);
pthread_attr_setstacksize(&attr, 4096);

param.sched_priority = 100;
pthread_attr_setschedparam(&attr, &param);

pthread_create(&tid, &attr, worker_thread, NULL);
```

## 취소

TizenRT에는 task와 pthread 모두 cancellation 관련 flag가 있다. 다만 API 모델은 다르다.

- 작업: `task_delete()`, `task_setcancelstate()`, `task_setcanceltype()`, `task_testcancel()`
- p스레드: `pthread_cancel()`, `pthread_setcancelstate()`, `pthread_setcanceltype()`, `pthread_testcancel()`

`task_delete()`도 cancellation disabled/deferred 상태를 확인한다. pthread는 POSIX cancellation 모델에 맞춰 cleanup handler와 join 처리가 함께 연결된다.

## 선택 기준

아래 기준으로 선택한다.

| 필요 조건 | 선택 |
|---|---|
| 독립 application 또는 shell command처럼 실행해야 한다 | task |
| 같은 application 내부 worker가 필요하다 | pthread |
| `argc/argv` 기반 entry가 자연스럽다 | task |
| `void *arg`와 `void *return`이 자연스럽다 | pthread |
| 종료 값을 기다리고 받아야 한다 | pthread |
| 반환값보다 독립 lifecycle과 재시작이 중요하다 | task |
| mutex/cond/key/cleanup 등 pthread 기능이 필요하다 | pthread |
| 별도 task group과 child 관계가 필요하다 | task |
| parent task group의 리소스를 공유해야 한다 | pthread |

## 수정할 때 보는 파일

| 관심사 | 파일 |
|---|---|
| 공개 작업 API | `os/include/sched.h` |
| 공용 pthread API | `os/include/pthread.h` |
| TCB 타입과 flag | `os/include/tinyara/sched.h` |
| pthread 기본 attribute | `os/include/tinyara/pthread.h` |
| task 생성 | `os/kernel/task/task_create.c` |
| task entry 실행 | `os/kernel/task/task_start.c` |
| task 삭제 | `os/kernel/task/task_delete.c` |
| 공통 종료 경로 | `os/kernel/task/task_exit.c`, `os/kernel/task/task_terminate.c` |
| pthread 생성 | `os/kernel/pthread/pthread_create.c` |
| pthread 종료 | `os/kernel/pthread/pthread_exit.c` |
| pthread 조인/detach | `os/kernel/pthread/pthread_join.c`, `os/kernel/pthread/pthread_join_internal.c`, `os/kernel/pthread/pthread_detach.c`, `os/kernel/pthread/pthread_completejoin.c` |
| task group 처리 | `os/kernel/group/`, `os/kernel/task/task_setup.c` |

## 실수하기 쉬운 부분

- `pthread_t`가 TizenRT에서는 `pid_t`라고 해서 task PID와 의미가 완전히 같다고 보면 안 된다. pthread에는 join info와 task group 소속 조건이 추가된다.
- `pthread_join()`으로 task를 기다릴 수 없다. task에는 join info가 없으므로 `EINVAL` 경로로 간다.
- `task_delete()`로 pthread를 관리하지 않는다. pthread는 `pthread_cancel()`과 `pthread_join()` 또는 `pthread_detach()`를 사용한다.
- joinable pthread를 만들고 join/detach를 하지 않으면 join 관리 정보가 남을 수 있다.
- task는 `argv[]`가 NULL로 끝나야 한다. `task_start()`는 NULL이 나올 때까지 인자를 센다.
- pthread 인자로 stack 변수 주소를 넘길 때는 parent 함수가 먼저 return하지 않도록 lifetime을 보장해야 한다.

## 요약

TizenRT에서 task와 pthread는 같은 scheduler 위에서 실행되지만 용도가 다르다. task는 독립 실행 단위이고, pthread는 같은 task group 안의 협업 thread다. 새 기능을 만들 때는 "이 작업이 독립 lifecycle을 가져야 하는가, 아니면 현재 application 내부 worker인가"를 먼저 판단하면 대부분의 선택이 정리된다.
