#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

#include <tinyara/mm/mm.h>

static int g_acquire_count;
static int g_release_count;
static int g_history_count;
static int g_irq_depth = 1;
static int g_irq_max;
static int g_irq_waitlock;

int up_interrupt_context(void)
{
	return 0;
}

struct mm_heap_s *mm_get_heap(void *address)
{
	return address;
}

int sem_init(sem_t *sem, int pshared, unsigned int value)
{
	(void)pshared;
	pthread_mutex_init(&sem->lock, NULL);
	pthread_cond_init(&sem->condition, NULL);
	sem->semcount = (int)value;
	sem->flags = 1;
	return 0;
}

int sem_wait(sem_t *sem)
{
	pthread_mutex_lock(&sem->lock);
	while (sem->semcount <= 0) {
		pthread_cond_wait(&sem->condition, &sem->lock);
	}
	sem->semcount--;
	pthread_mutex_unlock(&sem->lock);
	return 0;
}

int sem_trywait(sem_t *sem)
{
	g_irq_depth++;
	if (g_irq_depth > g_irq_max) {
		g_irq_max = g_irq_depth;
	}
	pthread_mutex_lock(&sem->lock);
	if (sem->semcount <= 0) {
		pthread_mutex_unlock(&sem->lock);
		g_irq_depth--;
		errno = EAGAIN;
		return -1;
	}
	sem->semcount--;
	g_acquire_count++;
	g_history_count++;
	pthread_mutex_unlock(&sem->lock);
	g_irq_depth--;
	return 0;
}

int sem_post(sem_t *sem)
{
	g_irq_depth++;
	if (g_irq_depth > g_irq_max) {
		g_irq_max = g_irq_depth;
	}
	pthread_mutex_lock(&sem->lock);
	sem->semcount++;
	g_release_count++;
	g_history_count++;
	pthread_cond_signal(&sem->condition);
	pthread_mutex_unlock(&sem->lock);
	g_irq_depth--;
	return 0;
}

static void reset_metrics(void)
{
	g_acquire_count = 0;
	g_release_count = 0;
	g_history_count = 0;
	g_irq_depth = 1;
	g_irq_max = 1;
	g_irq_waitlock = 0;
}

static void assert_unchanged(struct mm_heap_s *heap, pid_t holder, int count,
		int semcount, int expected)
{
	assert(mm_trysemaphore_fresh(heap) == expected);
	assert(heap->mm_holder == holder);
	assert(heap->mm_counts_held == count);
	assert(heap->mm_semaphore.semcount == semcount);
	assert(g_acquire_count == 0 && g_history_count == 0);
}

int main(void)
{
	struct mm_heap_s heap;
	pid_t self = getpid();

	mm_seminitialize(&heap);
	reset_metrics();
	heap.mm_holder = self;
	heap.mm_counts_held = 1;
	assert_unchanged(&heap, self, 1, 1, -EALREADY);

	reset_metrics();
	heap.mm_holder = self + 1;
	heap.mm_counts_held = 2;
	assert_unchanged(&heap, self + 1, 2, 1, -EBUSY);

	reset_metrics();
	heap.mm_holder = -1;
	heap.mm_counts_held = 1;
	assert_unchanged(&heap, -1, 1, 1, -EUCLEAN);

	reset_metrics();
	heap.mm_holder = -2;
	heap.mm_counts_held = 0;
	assert_unchanged(&heap, -2, 0, 1, -EUCLEAN);

	reset_metrics();
	heap.mm_holder = -1;
	heap.mm_counts_held = 0;
	heap.mm_semaphore.semcount = 2;
	assert_unchanged(&heap, -1, 0, 2, -EUCLEAN);

	reset_metrics();
	heap.mm_holder = -1;
	heap.mm_counts_held = 0;
	heap.mm_semaphore.semcount = 0;
	assert_unchanged(&heap, -1, 0, 0, -EBUSY);

	reset_metrics();
	heap.mm_semaphore.semcount = 1;
	assert(mm_trysemaphore_fresh(&heap) == 0);
	assert(heap.mm_holder == self && heap.mm_counts_held == 1);
	assert(heap.mm_semaphore.semcount == 0);
	assert(g_acquire_count == 1 && g_history_count == 1);
	assert(g_irq_depth == 1 && g_irq_max == 2 && g_irq_waitlock == 0);
	mm_givesemaphore(&heap);
	assert(heap.mm_holder == -1 && heap.mm_counts_held == 0);
	assert(heap.mm_semaphore.semcount == 1);
	assert(g_release_count == 1 && g_history_count == 2);
	assert(g_irq_depth == 1 && g_irq_max == 2 && g_irq_waitlock == 0);
	printf("MLC_TASK6_SEMAPHORE status=PASS variant=%s production_mm_sem=true accounting_callbacks=instrumented irqwaitlock=0\n",
#ifdef CONFIG_SMP
		"smp_irqcount"
#elif defined(CONFIG_IRQCOUNT)
		"up_irqcount"
#else
		"up_no_irqcount"
#endif
	);
	return 0;
}
