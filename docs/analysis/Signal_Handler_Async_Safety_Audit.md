# Signal handler 내 비동기 안전성 검토

검토일: 2026-09-17\
기준: `master`, `29d2ed503c2915bd123ff020f5c76353aa19f34c`\
목적: timeout/signal 경로의 MQ 사용과 직접·간접 MM 재진입, 대기, 종료, 로그 호출 확인

## 1. 결론과 검토 범위

**signal handler 안에서 MQ를 보내는 코드가 실제로 존재한다.** Binary Manager 상태 callback의 응답 전송과 MM signal stress test에서 직접 확인했고, Task Manager의 unicast callback → reply API → MQ 전송 경로도 확인했다. `mq_send()`는 POSIX가 async-signal-safe로 보장하는 함수가 아니며, 이 TizenRT 구현에서는 실제로 동적 할당과 대기를 수행할 수 있다.

다만 사용자가 기억한 특정 `timeout handler`의 이름은 아직 특정하지 못했다. 확인한 커널 POSIX timer의 `timer_timeout()`은 MQ를 보내는 함수가 아니라 signal을 전달하는 ISR callback이다. AIFW timer는 signal에서 사용자 callback을 직접 실행하지만, timer 구현 자체에 MQ 전송은 없다. Eventloop/WPA/ST Things의 일반 timeout callback은 실행 문맥이 다르다.

검색 범위는 `apps`, `framework`, `external`, `os`, `lib`의 C/C++ 소스·헤더다. `sigaction`, `signal`, `sigset`, `sig_sethandler`, handler 대입문을 검색하고 주석·문자열을 제외했다. 후보 162개 파일 중 105개 파일에서 235개 비주석 패턴을 얻었다. 이 수에는 API 선언, proxy, handler 해제, 비교문도 포함되며 **235개 handler 또는 235개 결함이라는 뜻이 아니다.**

TizenRT framework·kernel·libc의 확인된 handler와 MQ·signal 공통 함수를 직접 읽었다. 테스트·외부 예제에서는 등록된 이름으로 찾은 55개 handler 본문도 분류했다. 전체 검색 목록은 [등록 지점 목록](signal-handler-audit/registration-inventory.tsv), 테스트·샘플의 본문 발췌는 [샘플 검토 자료](signal-handler-audit/sample-handler-bodies.txt)에 있다.

이는 정적 호출 경로 감사다. 사용자에게서 전달받는 임의의 함수 포인터, 저장소 밖의 앱·prebuilt 코드, 모든 제품별 전처리 결과까지 안전하다고 증명한 검토는 아니다. 실제 장애 보드의 재현이나 이번 MM assert의 직접 원인 확정도 수행하지 않았다.

## 2. 판정 기준

POSIX의 async-signal-safe 보장 목록을 기준으로 함수 호출을 분류했다. 목록에 없다는 사실과 이 구현에서 실제로 allocator/lock에 진입한다는 사실을 구분한다. 구현이 별도 확장을 제공할 수 있지만, 이 검토에서 문제가 된 경로에는 메모리 재진입·대기가 실제로 존재한다. [POSIX.1-2024 §2.4.3](https://pubs.opengroup.org/onlinepubs/9799919799/functions/V2_chap02.html#tag_16_04_03)

| 종류 | 이 검토에서의 판단 |
| --- | --- |
| `malloc/calloc/realloc/free`, C++ 동적 객체·컨테이너 변경 | 비동기 handler에서 안전하다고 가정할 수 없음 |
| `mq_send/mq_timedsend/mq_receive/mq_open/mq_close/mq_getattr/mq_notify/mq_unlink` | async-signal-safe 보장 없음. `O_NONBLOCK`이어도 이 판정은 바뀌지 않음 |
| `printf/fprintf/snprintf/syslog`, 이를 호출하는 로그 macro | 보장 없음. 실제 설정에서 macro가 제거되는지 구분 |
| `sem_wait`, 일반 mutex lock, `pthread_join` | 보장 없음. 중단한 실행 경로가 자원을 쥐고 있으면 자기 대기 가능 |
| `pthread_cancel`, `pthread_exit`, `exit` | async-signal-safe 함수로 취급하지 않음. async-cancel-safe와 구분 |
| 일반 사용자 callback | 호출 자체만으로 위반 확정은 아니지만, callback 본문에도 signal 제약이 그대로 적용됨 |
| `sem_post`, `write`, `read`, `open`, `close`, `sigaction`, `sigprocmask`, `sigqueue`, `sigpause` | POSIX 보장 또는 해당 사양에 따른 함수. 이름만 보고 금지 목록에 넣지 않음. 인자·공유 데이터·대기 설계는 별도 검토 |
| RTOS의 IRQ lock, scheduler lock, 내부 종료 함수 | POSIX 애플리케이션 API 판정과 분리. 내부 재진입 및 시스템 일관성을 검토 |

`sem_post()`는 signal handler에서 사용할 수 있는 함수다. 하지만 저장할 이벤트 데이터의 소유권·동기화까지 자동으로 해결하지는 않는다. [POSIX sem_post](https://pubs.opengroup.org/onlinepubs/009695399/functions/sem_post.html)

MQ가 가득 찼을 때 blocking descriptor의 `mq_send()`가 대기하고, `O_NONBLOCK`이면 오류로 반환하는 것은 MQ 사양 자체의 동작이다. **비차단 설정은 동적 할당이나 재진입 안전성을 보장하는 설정이 아니다.** [POSIX mq_send](https://pubs.opengroup.org/onlinepubs/9699919799/functions/mq_send.html)

## 3. MQ가 MM으로 들어가는 실제 경로

### 3.1 전송: pool 고갈 시 할당

```text
signal handler
  → mq_send()
      → enter_cancellation_point()
      → 큐가 가득 차면 mq_waitsend()
      → mq_msgalloc()
          → g_msgfree에 여유가 있으면 기존 객체 사용
          → 여유가 없고 ISR이 아니면 kmm_malloc()
              → mm_takesemaphore()
```

- [mq_send.c:149](../../os/kernel/mqueue/mq_send.c#L149): cancellation point 진입.
- [mq_send.c:168](../../os/kernel/mqueue/mq_send.c#L168): 큐 상태 확인 및 `mq_waitsend`, 175행에서 `mq_msgalloc`.
- [mq_sndinternal.c:190](../../os/kernel/mqueue/mq_sndinternal.c#L190): ISR인지 구분한다. signal handler 여부를 검사하는 분기가 아니다.
- [mq_sndinternal.c:217](../../os/kernel/mqueue/mq_sndinternal.c#L217): 일반 문맥에서 pool이 비면 `kmm_malloc`.
- [mq_sndinternal.c:286](../../os/kernel/mqueue/mq_sndinternal.c#L286): `O_NONBLOCK` 처리, 313행에서 실제 task block.

**signal handler는 수신 스레드 문맥에서 실행되므로 ISR용 예비 pool 분기를 자동으로 타지 않는다.** ISR에서 MQ 전송을 지원한다는 사실로 signal에서의 MQ 전송 안전성을 판단하면 안 된다.

로컬 생성 설정의 `CONFIG_PREALLOC_MQ_MSGS`는 4다. 이는 공용 메시지 pool의 설정이며 개별 큐의 `mq_maxmsg`와 다르다. 큐에 공간이 남아도 다른 큐에서 공용 pool을 사용 중이면 동적 할당으로 갈 수 있다. 실제 장애 제품의 설정이 같다는 정보는 없다.

### 3.2 수신: 동적 메시지 해제

```text
signal handler
  → mq_receive()
      → mq_waitreceive()              [큐가 비면 대기 가능]
      → mq_doreceive()
          → mq_msgfree()
              → MQ_ALLOC_DYN이면 sched_kfree()
                  → 즉시 해제 가능한 것으로 판단하면 kmm_free()
```

근거: [mq_receive.c:145](../../os/kernel/mqueue/mq_receive.c#L145), [mq_rcvinternal.c:294](../../os/kernel/mqueue/mq_rcvinternal.c#L294), [mq_msgfree.c:143](../../os/kernel/mqueue/mq_msgfree.c#L143), [sched_free.c:156](../../os/kernel/sched/sched_free.c#L156).

`sched_kfree()`라는 이름만으로 지연 해제라고 생각하면 안 된다. ISR이 아니고 try가 성공하면 즉시 free한다. 같은 PID의 MM 보유 상태에서는 재귀 try가 성공하므로, 이전에 분석한 MM 재진입 경로와 연결될 수 있다.

`mq_getattr()`로 메시지가 있는지 확인한 뒤 `mq_receive()`를 호출하는 코드도 있다. 다른 수신자가 존재한다면 확인과 수신 사이에 메시지가 없어질 수 있다. 이 확인만으로 비차단 처리가 보장되지는 않는다.

### 3.3 open: 기존 큐도 descriptor 할당 가능

[mq_open.c:164](../../os/fs/mqueue/mq_open.c#L164)는 이미 존재하는 큐에 대해서도 `mq_descreate()`를 호출한다. descriptor pool이 비면 [mq_descreate.c:124](../../os/kernel/mqueue/mq_descreate.c#L124) → [mq_initialize.c:226](../../os/kernel/mqueue/mq_initialize.c#L226)의 `kmm_malloc()`으로 간다. 따라서 `O_CREAT`을 쓰지 않았다고 할당 가능성이 사라지지 않는다.

### 3.4 취소가 함께 발생할 때

`CONFIG_CANCELLATION_POINTS`가 켜진 경우 handler의 MQ 호출은 취소 처리 지점이 된다. [task_cancelpt.c:244](../../os/kernel/task/task_cancelpt.c#L244)는 pending cancellation이 있으면 `pthread_exit()` 또는 `exit()`로 연결한다. 중단된 원래 코드가 MM을 가진 상태라면 종료 중 lock 회수 문제와도 연결될 수 있다.

로컬 설정에서는 이 옵션이 꺼져 있다. 이 효과를 현재 로컬 빌드에서 실행된 사실로 주장하지 않는다.

## 4. TizenRT 실행 경로별 발견 사항

### F1. Binary Manager 상태 callback — MQ 수신·사용자 callback·MQ 응답을 signal에서 실행

- 등록: [binary_manager_register_callback.c:134](../../framework/src/binary_manager/binary_manager_register_callback.c#L134), 139행 `sigaction(SIGBM_STATE, ...)`.
- handler: 같은 파일의 [62행](../../framework/src/binary_manager/binary_manager_register_callback.c#L62).
- 수행: `snprintf` 80행, `mq_open` 81행, `mq_getattr` 88행, `mq_receive` 92행, 사용자 callback 96행, 응답 전송 helper 99행, `mq_close` 110행.
- 응답 helper는 [45행](../../framework/src/binary_manager/binary_manager_register_callback.c#L45)에서 `mq_open(O_WRONLY)`, 51행에서 `mq_send`, 57행에서 `mq_close`한다.
- 오류 분기의 `bmdbg`는 설정에 따라 `syslog` 또는 `logm`으로 간다.

**handler의 정상 처리만으로도 MQ의 간접 MM 할당·해제가 가능하다.** 응답 큐에는 `O_NONBLOCK`이 없어 가득 차면 대기할 수 있다. 사용자 callback은 전달받은 함수 포인터이므로 추가 위험은 등록한 앱의 본문을 확인해야 한다.

이 경로는 `CONFIG_BINARY_MANAGER`와 callback 등록·상태 통지에 관련된다. 앞서 이슈 3에서 본 `CONFIG_BINMGR_RECOVERY`의 중복 반환 문제와는 별개다. recovery 옵션이 꺼져 있어도 이 callback 경로까지 없어지는 것은 아니다.

### F2. Task Manager unicast/broadcast — 직접 malloc/free

- 등록: [task_manager_set_callback.c:213](../../framework/src/task_manager/task_manager_set_callback.c#L213), 258행.
- handler: [taskmgr_msg_cb(), 48행](../../framework/src/task_manager/task_manager_set_callback.c#L48).
- 직접 `TM_ALLOC`: 65, 81, 87행. 직접 `TM_FREE`: 71, 74, 107, 111, 115, 119행 등.
- 사용자 callback: 73, 104행.
- [task_manager_internal.h:75](../../framework/src/task_manager/task_manager_internal.h#L75)의 `TM_ALLOC/TM_FREE`는 `malloc/free`다.

signal callback이 `task_manager_reply_unicast()`를 호출하는 사용 예도 저장소에 있다. `apps/examples/testcase/ta_tc/task_manager/utc/utc_taskmanager_main.c:90`, `itc/itc_taskmanager_main.c:568`이 해당한다. reply API는 [task_manager_unicast.c:147](../../framework/src/task_manager/task_manager_unicast.c#L147)에서 MQ를 전송하며, 그 전에 할당·큐 열기 등을 수행한다.

즉 프레임워크 wrapper 자체의 MM 사용과 callback에서 호출하는 reply API 양쪽을 일반 스레드 문맥으로 이동해야 한다.

### F3. Task Manager 종료 callback — free, ioctl, 다른 task 종료

- [taskmgr_stop_cb(), 122행](../../framework/src/task_manager/task_manager_set_callback.c#L122): 사용자 callback 호출 후 `TM_FREE`, 마지막 `sigqueue`.
- [taskmgr_update_stop_status(), 431행](../../framework/src/task_manager/task_manager_core.c#L431): `taskmgr_handle_tcb(TMIOC_TERMINATE)` 및 455행 `TM_FREE`.
- [taskmgr_handle_tcb(), 321행](../../framework/src/task_manager/task_manager_core.c#L321)는 `ioctl`을 호출하고, 드라이버의 [TMIOC_TERMINATE 분기](../../os/drivers/task_manager/task_manager_drv.c#L261)는 실제 task/pthread 종료로 연결된다.
- 등록은 [SET_TERMINATION_CB macro](../../framework/src/task_manager/task_manager_internal.h#L237) 및 각 호출 지점이다.

`sigqueue()` 자체를 금지 호출로 분류하지 않는다. 문제는 그 전에 수행하는 메모리 해제·종료·사용자 callback이다.

별도의 [taskmgr_pause_handler()](../../framework/src/task_manager/task_manager_core.c#L359)는 `sigpause()`로 의도적으로 멈춘다. `sigpause` 호출을 POSIX 비동기 불안전 함수라고 단정하지 않는다. 다만 원래 스레드가 자원을 보유한 채 정지할 수 있다는 suspend 설계의 위험은 남는다.

### F4. Preference 변경 callback — MQ 수신·로그·사용자 callback

- 등록: [private_preference.c:291](../../framework/src/preference/private_preference.c#L291), [shared_preference.c:296](../../framework/src/preference/shared_preference.c#L296).
- handler: [preference_signal_cb()](../../framework/src/preference/preference_callback.c#L31).
- 수행: `snprintf` 46행, `mq_open` 47행, `mq_getattr` 54행, `mq_receive` 58행, 사용자 callback 61행, `mq_close/mq_unlink` 66–67행.

직접 `malloc`이 보이지 않아도 MQ descriptor 생성과 동적 메시지 회수에서 kernel MM으로 들어갈 수 있다. 로그는 오류 분기·디버그 설정에 따라 추가된다.

### F5. Messaging IPC — signal 안의 메시지 파싱 전체

- `messaging_set_notify_signal()`이 handler를 등록한다. [messaging_common.c:90](../../framework/src/messaging/messaging_common.c#L90).
- 실제 handler는 [messaging_run_callback(), 184행](../../framework/src/messaging/messaging_common.c#L184)이다.
- `mq_getattr` 205행, `MSG_ALLOC` 213행, `mq_receive` 219행, 사용자 callback 233행, `MSG_FREE` 234행, `mq_close/mq_unlink` 237–241행, notification 재등록 255행 등을 실행한다.
- `MSG_ALLOC/MSG_FREE`는 [messaging_internal.h:29](../../framework/src/messaging/messaging_internal.h#L29)의 `malloc/free`다. notification helper도 `mq_notify()`를 호출한다.

메시지 처리 루프가 signal 문맥에 통째로 들어가 있다. 수신·파싱·응답·사용자 callback을 전용 worker에서 처리하도록 경계를 옮기는 것이 필요하다.

### F6. AIFW POSIX timer — timer thread를 만들지만 사용자 callback은 signal에서 실행

- [aifw_timer.cpp:164](../../framework/src/aifw/aifw_timer.cpp#L164): `aifw_timer_cb`를 handler로 등록.
- 177–180행: `SIGEV_SIGNAL` timer 생성.
- [235행](../../framework/src/aifw/aifw_timer.cpp#L235): handler에서 로그 macro 및 252행 `timer->function()` 호출.
- 실제 framework 연결: [AIModelService.cpp:186](../../framework/src/aifw/AIModelService.cpp#L186)의 `timerTaskHandler` → `mCollectRawDataCallback`.
- 로그가 활성화되면 [aifw_log.h:36](../../framework/include/aifw/aifw_log.h#L36)의 `PRINT_LOG`는 `printf`를 여러 번 호출한다.

타이머 전용 pthread가 있다는 사실과 callback이 일반 스레드 문맥에서 실행된다는 사실은 다르다. 현재는 그 pthread에 도착한 signal handler가 callback을 실행한다. framework timer 자체에는 `mq_send`가 없다.

저장소의 구체적인 callback도 추적했다. [aifw_test_main.cpp:239](../../apps/examples/aifw_test/aifw_test_main.cpp#L239)의 `sine_collectRawDataListener`가 실제로 등록되어 다음 작업을 수행한다.

```text
aifw_timer_cb → timerTaskHandler → sine_collectRawDataListener
  → readCSVData → getCSVLine → readLine → fgets
  → ai_helper_push_data → AIModelService::pushData
      → AIInferenceHandler::pushData → new[]
      → AIModel::pushData → AIDataBuffer::writeData → pthread_mutex_lock
  → 오류 시 aifw_test_deinit → free, CSV 정리, pthread_create
```

근거: [aifw_csv_reader_utils.c:26](../../framework/src/aifw/aifw_csv_reader_utils.c#L26), [AIInferenceHandler.cpp:65](../../framework/src/aifw/AIInferenceHandler.cpp#L65), [AIModel.cpp:602](../../framework/src/aifw/AIModel.cpp#L602), [AIDataBuffer.cpp:302](../../framework/src/aifw/AIDataBuffer.cpp#L302), [aifw_test_main.cpp:206](../../apps/examples/aifw_test/aifw_test_main.cpp#L206). 따라서 이 예제에서는 callback이 위험할 수 있다는 가능성뿐 아니라 실제 stdio·동적 할당·mutex 호출 연결까지 확인됐다.

반면 `SoftwareEndPointDetector.cpp:46`이 전달하는 수집 callback은 빈 lambda다. 모든 AIFW 사용자가 같은 작업을 한다고 일반화하지 않는다. 저장소 밖 제품의 수집 callback은 별도 확인이 필요하다.

### F7. libtuv TinyAra signal bridge — handler에서 sem_wait

- 등록: [uv_tinyara_signal.c:195](../../external/libtuv/source/tinyara/uv_tinyara_signal.c#L195).
- [handler 143행](../../external/libtuv/source/tinyara/uv_tinyara_signal.c#L143)의 154행에서 `uv__signal_lock()` 호출.
- [90행](../../external/libtuv/source/tinyara/uv_tinyara_signal.c#L90)의 실제 구현은 `sem_wait(&uv_sig_sem)`이다.

일반 경로에서 signal을 차단하고 lock을 얻는 보호가 있더라도 `sem_wait`가 POSIX async-signal-safe 함수가 되는 것은 아니다. 다만 그 보호 때문에 “같은 스레드가 반드시 자기 lock을 기다린다”고 단정할 수는 없다. 경쟁·대기 가능 조건은 별도 확인 대상이다.

메시지를 넘기는 `write()`와 마지막 `sem_post()` 자체는 금지 함수로 분류하지 않는다. pipe는 228행에서 비차단 옵션으로 생성된다. `external/iotjs/deps/libtuv/src/unix/signal.c`의 lock은 pipe `read/write` 방식이므로 TinyAra semaphore 버전과 혼동하지 않는다.

### F8. libc AIO completion — signal handler에서 lib_free

[lio_sighandler()](../../lib/libc/aio/lio_listio.c#L178)는 완료 확인 후 [226행](../../lib/libc/aio/lio_listio.c#L226)에서 `lib_free(sighand)`를 실행한다. [lib_internal.h:111](../../lib/libc/lib_internal.h#L111) 및 129행에서 이 macro는 빌드에 따라 `kmm_free` 또는 `free`로 연결된다. `sched_lock()`을 잡았다는 사실만으로 signal 재진입 시 allocator 안전성이 보장되지는 않는다. 등록은 311–317행이며 `CONFIG_FS_AIO`에 따른 조건부 경로다.

### F9. 커널 signal 공통 후처리 — 사용자 handler가 비어 있어도 MM 접근 가능

```text
sig_deliver()
  → 사용자 handler 실행
  → sig_unmaskpendingsignal()
      → sig_tcbdispatch() → sig_queueaction()
          → pool 부족 시 kmm_malloc()
      → 동적 pending signal 해제 → sched_kfree()
  → sig_releasependingsigaction()
      → SIG_ALLOC_DYN이면 sched_kfree()
```

근거: [sig_deliver.c:209](../../os/kernel/signal/sig_deliver.c#L209), [sig_unmaskpendingsignal.c:146](../../os/kernel/signal/sig_unmaskpendingsignal.c#L146), [sig_allocatependingsigaction.c:133](../../os/kernel/signal/sig_allocatependingsigaction.c#L133), [sig_releasependingsignal.c:141](../../os/kernel/signal/sig_releasependingsignal.c#L141), [sig_releasependingsigaction.c:130](../../os/kernel/signal/sig_releasependingsigaction.c#L130).

これは 애플리케이션이 금지 함수를 호출하는 경우와 다른 **OS 내부 구현 문제**다. 앱 callback에서 MQ와 malloc을 모두 제거하더라도 이 경로의 조건부 MM 재진입 문제는 남는다. 이전 문서의 2번 이슈와 직접 관련된다.

### F10. 커널 기본 종료 signal 처리 — 취소·종료·join

[task_activate.c:123](../../os/kernel/task/task_activate.c#L123)이 설치한 [thread_termination_handler()](../../os/kernel/task/task_terminate.c#L274)는 `task_delete()` 또는 자기 PID의 `pthread_cancel()` 후 `pthread_join()`을 호출한다.

즉시 자기 취소가 일어나면 `pthread_cancel()`에서 반환하지 않으므로 뒤의 `pthread_join()`은 실행되지 않을 수 있다. 취소가 보류되는 조건까지 구분해야 한다. 이 경로는 POSIX 앱이 SIGKILL handler를 등록하는 일반 사례가 아니라 TizenRT의 내부 종료 구현이다. 중단된 MM 작업이 끝나기 전에 종료 정리가 수행되는지 검토해야 한다.

## 5. 시스템 유틸리티·테스트·외부 코드

| 위치 | 확인된 handler 동작 | 판정 / 범위 |
| --- | --- | --- |
| [CU sigint](../../apps/system/cu/cu_main.c#L135) | `pthread_cancel`, `close`, `exit` | cancel/exit가 문제. close 자체를 금지 함수로 분류하지 않음 |
| [RTC example alarm_handler](../../apps/examples/rtc/rtc_main.c#L198) | `open`, `ioctl`, `printf`, `close` | printf 및 일반 ioctl의 비동기 안전성 보장 없음. open/close는 구분 |
| [MM stress mm_signal_handler](../../apps/examples/testcase/le_tc/stress/stress_mm_sem_signal.c#L119) | `mq_send`, 조건부 `sem_post` | MQ 재진입을 유발하는 stress 코드. 이를 정상적인 POSIX 사용 예나 POSIX 준수 증거로 취급하면 안 됨 |
| [ostest signest](../../apps/examples/testcase/ostest/signest.c#L113) | handler 내 printf, ASSERT, scheduler helper 등 | 시험 코드에도 부적절한 출력이 있음. 재진입 시험 목적과 제품 코드 구분 |
| [ostest death_of_child](../../apps/examples/testcase/ostest/sighand.c#L83) | printf | signal 안의 stdio |
| [le_tc sigmask_handler](../../apps/examples/testcase/le_tc/kernel/tc_pthread.c#L110) | 오류 분기 printf | 조건부 호출도 포함 |
| [le_tc sigquit/sigint](../../apps/examples/testcase/le_tc/kernel/tc_signal.c#L60) | `TC_ASSERT_EQ` | macro의 시험 출력·오류 처리 경로 주의 |
| [libcoap SIGINT](../../apps/examples/libcoap/server/libcoap-server.c#L83), le_tc RTC/MQ callback, 다수 IoTivity 샘플 | flag 저장 또는 빈 함수 | 직접 unsafe 함수 없음. 전역 flag의 타입·공유 방식까지 안전하다고 보증한 것은 아님 |
| [IoTivity Linux ocserverslow AlarmHandler](../../external/iotivity/iotivity_1.2-rel/resource/csdk/stack/samples/linux/SimpleClientServer/ocserverslow.cpp#L277) | 로그, container pop, request 처리, `OICFree`, payload destroy | 외부 Linux 샘플의 별도 문제. TizenRT 장애 실행 경로로 입증하지 않음 |
| [TinyDTLS dtls_handle_signal](../../external/wakaama/examples/shared/tinydtls/tests/dtls-client.c#L232) | `dtls_free_context` 등 정리 | 외부 테스트의 동적 자원 정리 경로 |
| [Telegesis sigHandler](../../external/iotivity/iotivity_1.2-rel/plugins/zigbee_wrapper/telegesis_wrapper/src/telegesis_socket.c#L572) | OIC 로그 | 외부 플러그인, 실제 제품 포함 여부 별도 |
| [WPA Linux eloop_handle_alarm](../../external/wpa_supplicant/src/utils/eloop.c#L713) | wpa 로그, `exit` | `CONFIG_OS_LINUX` 분기. 일반 WPA timeout callback과 다름 |
| [curl alarmfunc](../../external/curl/hostip.c#L539) | `siglongjmp` | 함수 이름만으로 금지 판정하지 않음. unsafe 함수를 중단한 뒤의 후속 실행 제약과 해당 빌드 조건 확인 필요 |

샘플의 assert는 실패 시 출력·종료 코드가 실행될 수 있다. 반대로 단순 등록 함수 안에서 malloc하는 것을 handler의 malloc으로 잘못 분류하지 않았다. 예를 들어 Binary Manager callback 구조체의 등록 시점 malloc과 실제 signal 수신 시점의 MQ 간접 할당은 서로 다른 실행이다.

## 6. timeout이라는 이름만으로 signal이라고 판단하면 안 되는 경우

| 코드 | 실제 실행 문맥 | MQ / MM 해석 |
| --- | --- | --- |
| [kernel timer_timeout](../../os/kernel/timer/timer_settime.c#L182) | watchdog ISR | `timer_sigqueue → sig_dispatch`로 signal 생성. 이 함수에 MQ send 없음 |
| [AIFW aifw_timer_cb](../../framework/src/aifw/aifw_timer.cpp#L235) | POSIX signal handler | 사용자 callback까지 signal 문맥. 앞의 F6 적용 |
| [Eventloop timeout_callback_func](../../framework/src/eventloop/eventloop_timer.c#L115) | `uv_run → uv__run_timers`의 일반 실행 | 이 callback의 malloc/MQ를 signal 제약 위반으로 볼 근거 없음 |
| [WPA eloop timeout dispatch](../../external/wpa_supplicant/src/utils/eloop.c#L898) | event loop | `eloop_timeout_handler`라는 typedef가 있어도 signal handler가 아님 |
| [ST Things timeout process](../../framework/src/st_things/things_stack/utils/things_wait_handler.c#L214) | 237행에서 생성한 pthread | 일반 timeout thread의 callback |
| vendor 하드웨어 timer / work_queue callback | ISR 또는 worker, API마다 다름 | POSIX signal callback과 구분. ISR의 별도 호출 제약을 적용해야 함 |

## 7. 로컬 설정에 따른 우선순위

문서 작성 시 `os/.config`와 생성된 `os/include/tinyara/config.h`를 읽었다. 장애 펌웨어 설정은 여전히 미확인이다.

| 설정 | 로컬 값 | 영향 |
| --- | --- | --- |
| `CONFIG_BINARY_MANAGER` | 켜짐 | F1을 우선 점검. 실제 실행에는 callback 등록과 상태 통지가 필요 |
| `CONFIG_BINMGR_RECOVERY` | 활성 정의 없음 | F1의 상태 callback 위험까지 제외할 근거가 되지 않음 |
| `CONFIG_PREALLOC_MQ_MSGS` | 4 | 공용 메시지 pool 부족 시 동적 할당 경로 존재 |
| `CONFIG_SIGKILL_HANDLER` | 켜짐 | 종료 signal 경로 관련 |
| `CONFIG_CANCELLATION_POINTS` | 꺼짐 | MQ cancellation point를 통한 deferred 취소 효과는 로컬 적용 제외 |
| `CONFIG_TASK_MANAGER`, `CONFIG_MESSAGING_IPC`, `CONFIG_PREFERENCE` | 꺼짐 | 소스에 문제 패턴은 있으나 로컬 실행 원인으로 단정하지 않음 |
| `CONFIG_AIFW`, `CONFIG_LIBTUV`, `CONFIG_FS_AIO` | 꺼짐 | 해당 조건부 모듈도 동일 |
| `CONFIG_LOGM` | 꺼짐 | 활성화된 일반 debug macro는 syslog 방향. 로그 macro별 활성 여부는 별도 |

현재 증거로 우선순위를 매기면 **Binary Manager callback의 MQ 처리 → 공통 signal 후처리의 MM 접근 → 실제 앱의 타이머/signal callback 본문** 순이다. 이 순서는 이번 assert 원인의 확정 순위가 아니라 현재 소스·설정과 연결되는 조사 순서다.

## 8. 수정 원칙과 검증 항목

권장 구조는 다음과 같다.

```text
초기화 시: 큐·버퍼·semaphore·worker 준비
signal handler: 최소 이벤트 표시 + sem_post 또는 검증된 비차단 알림
worker: MQ 송수신, 메모리 할당·해제, 로그, 사용자 callback, 취소·종료 작업
```

타이머 전용 스레드가 이미 있는 AIFW는 signal을 동기적으로 기다린 뒤 일반 실행에서 callback을 부르는 구조도 검토할 수 있다. 단순히 스레드를 하나 더 만드는 것으로 끝내지 말고 callback 실행 위치를 바꿔야 한다.

이벤트가 중복·누적되는 의미, payload 수명, 큐 overflow 정책, signal 목적지 스레드, 종료 시 pending 이벤트와 worker 정리도 함께 정의해야 한다. 무조건적인 handler 내 mutex나 `mq_send(O_NONBLOCK)` 대체는 이 목적을 달성하지 못한다.

확인해야 할 회귀 조건:

- 공용 MQ pool 소진 상태에서도 signal 경로가 heap에 진입하지 않는지.
- MQ가 가득 찼거나 비었을 때 handler가 대기하지 않는지.
- 원래 스레드가 MM·stdio·다른 lock을 보유한 시점의 signal 전달.
- 동적 signal action/pending 객체가 사용된 뒤 사용자 handler가 비어 있어도 안전한지.
- exit/cancel과 callback 전달이 겹칠 때 객체 수명과 보류 이벤트 처리.
- 모듈별 설정 및 SMP/UP, protected user/kernel heap 구분.

이번 작업은 코드 검토와 문서화이며, 제품 소스 수정·보드 실행·새 장애 재현은 수행하지 않았다. 이전 MM 분석의 호스트 시험과 현재 정적 감사 결과를 같은 증거로 합쳐 주장하지 않는다.
