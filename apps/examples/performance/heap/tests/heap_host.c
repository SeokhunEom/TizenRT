/* Host functional/fault tests only: scheduling and CPU affinity are simulated.
 * No timing result from this harness is TizenRT performance evidence.
 */
#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <pthread.h>
#include <sched.h>
#include <semaphore.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static pthread_mutex_t accounting = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t scheduler = PTHREAD_MUTEX_INITIALIZER;
static int malloc_calls, malloc_ok, payload_frees, calloc_calls;
static int clock_calls, create_calls, joined, attr_calls, sem_calls;
static int fail_malloc, fail_calloc, fail_clock, fail_create, fail_attr, fail_sem;
static int mock_clock;
static uint64_t ticks;
static void *live[4096];
static bool payload[4096];

static void remember(void *p, bool is_payload)
{
	int i;
	if (!p) return;
	for (i = 0; i < 4096; i++) {
		if (!live[i]) { live[i] = p; payload[i] = is_payload; return; }
	}
	abort();
}
static void *test_malloc(size_t size)
{
	void *p;
	pthread_mutex_lock(&accounting);
	malloc_calls++;
	p = malloc_calls == fail_malloc ? NULL : malloc(size);
	if (p) malloc_ok++;
	remember(p, true);
	pthread_mutex_unlock(&accounting);
	return p;
}
static void *test_calloc(size_t n, size_t size)
{
	void *p;
	pthread_mutex_lock(&accounting);
	calloc_calls++;
	p = calloc_calls == fail_calloc ? NULL : calloc(n, size);
	remember(p, false);
	pthread_mutex_unlock(&accounting);
	return p;
}
static void test_free(void *p)
{
	int i;
	if (!p) return;
	pthread_mutex_lock(&accounting);
	for (i = 0; i < 4096 && live[i] != p; i++);
	assert(i < 4096);
	if (payload[i]) payload_frees++;
	live[i] = NULL;
	free(p);
	pthread_mutex_unlock(&accounting);
}
struct test_sem_s { pthread_mutex_t lock; pthread_cond_t cond; int count; int waiters; };
static int test_sem_init(struct test_sem_s *s, int shared, unsigned value)
{
	(void)shared;
	if (++sem_calls == fail_sem) { errno = ENOMEM; return -1; }
	pthread_mutex_init(&s->lock, NULL);
	pthread_cond_init(&s->cond, NULL);
	s->count = value; s->waiters = 0;
	return 0;
}
static int test_sem_destroy(struct test_sem_s *s)
{
	assert(!s->waiters);
	pthread_cond_destroy(&s->cond);
	pthread_mutex_destroy(&s->lock);
	return 0;
}
static int test_sem_wait(struct test_sem_s *s)
{
	pthread_mutex_lock(&s->lock);
	while (!s->count) {
		s->waiters++;
		pthread_cond_wait(&s->cond, &s->lock);
		s->waiters--;
	}
	s->count--;
	pthread_mutex_unlock(&s->lock);
	pthread_mutex_lock(&scheduler);
	pthread_mutex_unlock(&scheduler);
	return 0;
}
static int test_sem_post(struct test_sem_s *s)
{
	pthread_mutex_lock(&s->lock);
	s->count++;
	pthread_cond_signal(&s->cond);
	pthread_mutex_unlock(&s->lock);
	return 0;
}
static int test_sem_getvalue(struct test_sem_s *s, int *count)
{
	pthread_mutex_lock(&s->lock);
	*count = s->count - s->waiters;
	pthread_mutex_unlock(&s->lock);
	return 0;
}
static int test_sem_protocol(struct test_sem_s *s, int protocol)
{ (void)s; (void)protocol; return 0; }
static int test_clock(clockid_t id, struct timespec *ts)
{
	int result;
	pthread_mutex_lock(&accounting);
	clock_calls++;
	if (clock_calls == fail_clock) { pthread_mutex_unlock(&accounting); errno = EIO; return -1; }
	if (mock_clock) {
		ticks += 100;
		ts->tv_sec = ticks / 1000000000; ts->tv_nsec = ticks % 1000000000;
		result = 0;
	} else result = clock_gettime(id, ts);
	pthread_mutex_unlock(&accounting);
	return result;
}
static int test_attr(pthread_attr_t *a, int value)
{ (void)a; (void)value; return ++attr_calls == fail_attr ? EINVAL : 0; }
static int test_param(pthread_attr_t *a, const struct sched_param *p)
{ return test_attr(a, p->sched_priority); }
static int test_stack(pthread_attr_t *a, long size)
{ return test_attr(a, (int)size); }
static int test_create(pthread_t *t, const pthread_attr_t *a, void *(*f)(void *), void *arg)
{
	(void)a;
	if (++create_calls == fail_create) return EAGAIN;
	return pthread_create(t, NULL, f, arg);
}
static int test_join(pthread_t t, void **p)
{ int ret = pthread_join(t, p); if (!ret) joined++; return ret; }
static int test_min(int p) { (void)p; return 1; }
static int test_max(int p) { (void)p; return 255; }
#ifdef CONFIG_SMP
/* The target uses an integer cpuset. These tests verify configured placement,
 * not actual host placement or priority enforcement. */
static int test_affinity(pthread_attr_t *a, size_t size, const uint32_t *set)
{ (void)size; assert(*set == 1 || *set == 2); return test_attr(a, *set); }
#define cpu_set_t uint32_t
#undef CPU_ZERO
#undef CPU_SET
#define CPU_ZERO(s) (*(s) = 0)
#define CPU_SET(i,s) (*(s) |= 1U << (i))
#define pthread_attr_setaffinity_np test_affinity
#endif
#define malloc test_malloc
#define calloc test_calloc
#define free test_free
#define sem_t struct test_sem_s
#define sem_init test_sem_init
#define sem_destroy test_sem_destroy
#define sem_wait test_sem_wait
#define sem_post test_sem_post
#define sem_getvalue test_sem_getvalue
#define sem_setprotocol test_sem_protocol
#define SEM_PRIO_NONE 0
#define clock_gettime test_clock
#define pthread_create test_create
#define pthread_join test_join
#define pthread_attr_setinheritsched test_attr
#define pthread_attr_setschedpolicy test_attr
#define pthread_attr_setschedparam test_param
#define pthread_attr_setstacksize test_stack
#define sched_get_priority_min test_min
#define sched_get_priority_max test_max
#define sched_lock() pthread_mutex_lock(&scheduler)
#define sched_unlock() pthread_mutex_unlock(&scheduler)
#include "heap_performance_test.c"

static void self_test(void)
{
	struct heap_run_s run;
	uint64_t even[] = {40, 10, 30, 20};
	uint64_t odd[] = {30, 10, 20};
	int n;
	assert(heap_stats("even", even, 4) == 25);
	assert(heap_stats("odd", odd, 3) == 20);
	assert(heap_number("12x", 0, 100, &n) == EINVAL);
	assert(heap_number(" 12", 0, 100, &n) == EINVAL);
	assert(heap_number("99999999999999999999999", 0, 100, &n) == EINVAL);
	assert(heap_number("0", 0, 100, &n) == 0 && n == 0);
	memset(&run, 0, sizeof(run));
	run.options.blocks = 3; run.options.repeat = 2; run.size = 16;
	run.workers[0].run = &run;
	run.workers[0].data = calloc(3, sizeof(void *));
	mock_clock = 1;
	heap_measure(&run.workers[0]);
	assert(!run.workers[0].result.error);
	assert(run.workers[0].result.alloc_ns == 200);
	assert(run.workers[0].result.free_ns == 200);
	assert(run.workers[0].result.end - run.workers[0].result.begin == 500);
	free(run.workers[0].data);
}
int main(int argc, char **argv)
{
	int ret, i, remaining = 0;
#define ENV(name, dest) do { const char *s = getenv(name); if (s) dest = atoi(s); } while (0)
	ENV("FAIL_MALLOC", fail_malloc); ENV("FAIL_CALLOC", fail_calloc);
	ENV("FAIL_CLOCK", fail_clock); ENV("FAIL_CREATE", fail_create);
	ENV("FAIL_ATTR", fail_attr); ENV("FAIL_SEM", fail_sem);
	if (argc == 2 && !strcmp(argv[1], "selftest")) { self_test(); ret = 0; }
	else ret = heaptest_main(argc, argv);
	for (i = 0; i < 4096; i++) if (live[i]) remaining++;
	printf("HARNESS malloc_ok=%d frees=%d live=%d created=%d joined=%d\n", malloc_ok, payload_frees, remaining, create_calls - (fail_create && create_calls >= fail_create), joined);
	assert(!remaining && payload_frees == malloc_ok);
	return ret;
}
