/****************************************************************************
 * apps/examples/nuttx_mm_test/nuttx_mm_test_main.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Bounded integration checks for the NuttX allocator through TizenRT APIs.
 ****************************************************************************/

#include <tinyara/config.h>
#include <tinyara/irq.h>
#include <tinyara/mm/mm.h>
#include <tinyara/sched.h>
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <semaphore.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NXMM_VERSION "12.13.0"
#define NXMM_REVISION "b9b5d9afbf5a88f3c52830d4ce88ae038be0fd35"
#define NXMM_SLOTS 24
#define NXMM_STEPS 1024
#define NXMM_THREADS 4

struct nxmm_result_s {
	unsigned checks;
	unsigned failures;
	unsigned line;
};

struct nxmm_slot_s {
	unsigned char *mem;
	size_t size;
	uint32_t tag;
};

struct nxmm_worker_s {
	sem_t *start;
	uint32_t seed;
	struct nxmm_result_s result;
};

static int nxmm_check(struct nxmm_result_s *r, int ok, unsigned line)
{
	r->checks++;
	if (!ok) {
		r->failures++;
		if (!r->line) {
			r->line = line;
		}
	}
	return ok;
}

#define CHECK(r, expression) do { \
	if (!nxmm_check((r), !!(expression), __LINE__)) { \
		goto out; \
	} \
} while (0)

static uint32_t nxmm_random(uint32_t *seed)
{
	uint32_t x = *seed;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	*seed = x;
	return x;
}

static unsigned char nxmm_pattern(uint32_t tag, size_t pos)
{
	return (unsigned char)(tag ^ (pos * 29u) ^ (pos >> 3));
}

static void nxmm_fill(unsigned char *mem, size_t size, uint32_t tag)
{
	size_t i;
	for (i = 0; i < size; i++) {
		mem[i] = nxmm_pattern(tag, i);
	}
}

static int nxmm_matches(const unsigned char *mem, size_t size, uint32_t tag)
{
	size_t i;
	for (i = 0; i < size; i++) {
		if (mem[i] != nxmm_pattern(tag, i)) {
			return 0;
		}
	}
	return 1;
}

static void nxmm_public(struct nxmm_result_s *r)
{
	unsigned char *mem = NULL;
	unsigned char *zeros = NULL;
	unsigned char *aligned = NULL;
	unsigned char *resized;
	size_t alignment;
	size_t i;

	mem = malloc(73);
	CHECK(r, mem != NULL);
	nxmm_fill(mem, 73, 0x1234);
	zeros = calloc(11, 37);
	CHECK(r, zeros != NULL);
	for (i = 0; i < 11 * 37; i++) {
		CHECK(r, zeros[i] == 0);
	}
	resized = realloc(mem, 4097);
	CHECK(r, resized != NULL);
	mem = resized;
	CHECK(r, nxmm_matches(mem, 73, 0x1234));
	nxmm_fill(mem, 4097, 0xdef0);
	resized = realloc(mem, 19);
	CHECK(r, resized != NULL);
	mem = resized;
	CHECK(r, nxmm_matches(mem, 19, 0xdef0));
	resized = realloc(mem, 0);
	mem = resized;
	CHECK(r, mem == NULL);
	for (alignment = 8; alignment <= 4096; alignment *= 2) {
		aligned = memalign(alignment, alignment + 17);
		CHECK(r, aligned != NULL);
		CHECK(r, ((uintptr_t)aligned & (alignment - 1)) == 0);
		nxmm_fill(aligned, alignment + 17, alignment);
		CHECK(r, nxmm_matches(aligned, alignment + 17, alignment));
		free(aligned);
		aligned = NULL;
	}
	free(zeros);
	zeros = realloc(NULL, 97);
	CHECK(r, zeros != NULL);
	memset(zeros, 0x55, 97);
	free(NULL);

out:
	free(aligned);
	free(zeros);
	free(mem);
}

/* Simulate the fault diagnostic context on UP without provoking a panic.
 * The scheduler gate keeps the released heap idle for the second snapshot.
 * Assertions, result bookkeeping and output all occur after IRQ restoration
 * and releasing the heap/scheduler locks.
 */
static void nxmm_fault_diagnostics(struct nxmm_result_s *r, struct mm_heap_s *heap)
{
	struct tcb_s *self = sched_self();
	irqstate_t flags;
	bool saved_abort;
	bool held_unchanged;
	bool idle_unchanged;
	bool idle_available;
	pid_t holder;
	int held_count;
	int sem_count;
	int cancel_state;
	unsigned task_flags;
	int held_ret;
	int idle_ret;

	CHECK(r, self != NULL);
	if (!mm_takesemaphore(heap)) {
		nxmm_check(r, 0, __LINE__);
		return;
	}
	sched_lock();
	flags = irqsave();
	saved_abort = abort_mode;
	holder = heap->mm_holder;
	held_count = heap->mm_counts_held;
	sem_count = heap->mm_semaphore.semcount;
	cancel_state = heap->mm_cancel_state;
	task_flags = self->flags;
	abort_mode = true;
	held_ret = mm_check_heap_corruption(heap);
	held_unchanged = heap->mm_holder == holder &&
		heap->mm_counts_held == held_count &&
		heap->mm_semaphore.semcount == sem_count &&
		heap->mm_cancel_state == cancel_state && self->flags == task_flags;
	abort_mode = saved_abort;
	irqrestore(flags);
	mm_givesemaphore(heap);

	flags = irqsave();
	saved_abort = abort_mode;
	holder = heap->mm_holder;
	held_count = heap->mm_counts_held;
	sem_count = heap->mm_semaphore.semcount;
	cancel_state = heap->mm_cancel_state;
	task_flags = self->flags;
	idle_available = held_count == 0 && sem_count == 1;
	abort_mode = true;
	idle_ret = mm_check_heap_corruption(heap);
	idle_unchanged = heap->mm_holder == holder &&
		heap->mm_counts_held == held_count &&
		heap->mm_semaphore.semcount == sem_count &&
		heap->mm_cancel_state == cancel_state && self->flags == task_flags;
	abort_mode = saved_abort;
	irqrestore(flags);
	sched_unlock();

	CHECK(r, held_ret == -EAGAIN);
	CHECK(r, held_unchanged);
	CHECK(r, idle_available);
	CHECK(r, idle_ret == OK);
	CHECK(r, idle_unchanged);
out:
	return;
}

/* The public wrappers intentionally assert on OOM in the QEMU recipe.
 * Exercise expected failure directly through the mm API without changing
 * that product policy. No SIZE_MAX realloc is used: upstream debug builds
 * deliberately panic on overflow while rounding allocation size.
 */
static void nxmm_failure(struct nxmm_result_s *r)
{
	struct mm_heap_s *heap;
	struct mallinfo info;
	unsigned char *mem = NULL;
	void *unexpected = NULL;
	size_t impossible;
	volatile size_t count = SIZE_MAX / 2 + 1;

	mem = malloc(101);
	CHECK(r, mem != NULL);
	heap = mm_get_heap(mem);
	CHECK(r, heap != NULL);
	CHECK(r, mm_mallinfo(heap, &info) == 0);
	CHECK(r, info.arena > 0 && (size_t)info.arena < SIZE_MAX - 4096);
	impossible = (size_t)info.arena + 4096;
	nxmm_fill(mem, 101, 0xface);
	unexpected = mm_calloc(heap, count, 2, NULL);
	CHECK(r, unexpected == NULL);
	unexpected = mm_realloc(heap, mem, impossible, NULL);
	if (unexpected != NULL) {
		/* realloc success owns/replaces mem even if it violates expectation. */
		mem = unexpected;
		unexpected = NULL;
		nxmm_check(r, 0, __LINE__);
		goto out;
	}
	CHECK(r, nxmm_matches(mem, 101, 0xface));
	nxmm_fault_diagnostics(r, heap);
	CHECK(r, nxmm_matches(mem, 101, 0xface));

out:
	free(unexpected);
	free(mem);
}

static void nxmm_multiheap(struct nxmm_result_s *r)
{
	unsigned char *mem = NULL;
	unsigned char *aligned = NULL;
	unsigned char *resized;
	int index;

	/* The example's Kconfig requires at least two independently selected
	 * heaps. Silently using single-heap malloc_at macros cannot pass it.
	 */
	CHECK(r, CONFIG_KMM_NHEAPS > 1);
	for (index = 0; index < CONFIG_KMM_NHEAPS; index++) {
		mem = calloc_at(index, 7, 43);
		CHECK(r, mem != NULL);
		CHECK(r, mm_get_index_of_heap(mem) == index);
		{
			size_t i;
			for (i = 0; i < 7 * 43; i++) {
				CHECK(r, mem[i] == 0);
			}
		}
		nxmm_fill(mem, 7 * 43, 0x6000 + index);
		resized = realloc_at(index, mem, 2051);
		CHECK(r, resized != NULL);
		mem = resized;
		CHECK(r, mm_get_index_of_heap(mem) == index);
		CHECK(r, nxmm_matches(mem, 7 * 43, 0x6000 + index));
		aligned = memalign_at(index, 256, 379);
		CHECK(r, aligned != NULL);
		CHECK(r, ((uintptr_t)aligned & 255) == 0);
		CHECK(r, mm_get_index_of_heap(aligned) == index);
		nxmm_fill(aligned, 379, 0xc000 + index);
		CHECK(r, nxmm_matches(aligned, 379, 0xc000 + index));
		free(aligned);
		aligned = NULL;
		free(mem);
		mem = malloc_at(index, 67);
		CHECK(r, mem != NULL);
		CHECK(r, mm_get_index_of_heap(mem) == index);
		free(mem);
		mem = NULL;
	}

out:
	free(aligned);
	free(mem);
}

static void nxmm_fragment(struct nxmm_result_s *r, uint32_t seed)
{
	struct nxmm_slot_s slots[NXMM_SLOTS] = {{0}};
	unsigned step;
	unsigned i;

	for (step = 0; step < NXMM_STEPS; step++) {
		struct nxmm_slot_s *slot = &slots[nxmm_random(&seed) % NXMM_SLOTS];
		size_t newsize = 1 + nxmm_random(&seed) % 2048;
		unsigned action = nxmm_random(&seed) % 3;
		unsigned char *resized;

		/* Check every live allocation before changing the layout. */
		for (i = 0; i < NXMM_SLOTS; i++) {
			if (slots[i].mem) {
				CHECK(r, nxmm_matches(slots[i].mem, slots[i].size, slots[i].tag));
			}
		}
		if (!slot->mem) {
			slot->mem = malloc(newsize);
			CHECK(r, slot->mem != NULL);
			slot->size = newsize;
			slot->tag = nxmm_random(&seed);
			nxmm_fill(slot->mem, slot->size, slot->tag);
		} else if (action == 0) {
			free(slot->mem);
			slot->mem = NULL;
		} else {
			resized = realloc(slot->mem, newsize);
			CHECK(r, resized != NULL);
			slot->mem = resized;
			CHECK(r, nxmm_matches(slot->mem,
				slot->size < newsize ? slot->size : newsize, slot->tag));
			slot->size = newsize;
			nxmm_fill(slot->mem, slot->size, slot->tag);
		}
		if ((step & 15) == 0) {
			sched_yield();
		}
	}

out:
	for (i = 0; i < NXMM_SLOTS; i++) {
		if (slots[i].mem) {
			nxmm_check(r, nxmm_matches(slots[i].mem, slots[i].size, slots[i].tag), __LINE__);
			free(slots[i].mem);
		}
	}
}

static void *nxmm_worker(void *arg)
{
	struct nxmm_worker_s *worker = arg;
	int ret;
	do {
		ret = sem_wait(worker->start);
	} while (ret < 0 && errno == EINTR);
	if (nxmm_check(&worker->result, ret == 0, __LINE__)) {
		nxmm_fragment(&worker->result, worker->seed);
	}
	return NULL;
}

static void nxmm_threaded(struct nxmm_result_s *r)
{
	struct nxmm_worker_s workers[NXMM_THREADS] = {{0}};
	pthread_t threads[NXMM_THREADS];
	pthread_attr_t attr;
	sem_t start;
	unsigned created = 0;
	unsigned i;
	int ret;

	if (!nxmm_check(r, sem_init(&start, 0, 0) == 0, __LINE__)) {
		return;
	}
	ret = pthread_attr_init(&attr);
	if (!nxmm_check(r, ret == 0, __LINE__)) {
		sem_destroy(&start);
		return;
	}
	if (nxmm_check(r, pthread_attr_setstacksize(&attr, 8192) == 0, __LINE__)) {
		for (i = 0; i < NXMM_THREADS; i++) {
			workers[i].start = &start;
			workers[i].seed = 0x13579bdfu ^ ((i + 1) * 0x1020304u);
			ret = pthread_create(&threads[i], &attr, nxmm_worker, &workers[i]);
			if (!nxmm_check(r, ret == 0, __LINE__)) {
				break;
			}
			created++;
		}
	}
	pthread_attr_destroy(&attr);
	for (i = 0; i < created; i++) {
		nxmm_check(r, sem_post(&start) == 0, __LINE__);
	}
	for (i = 0; i < created; i++) {
		ret = pthread_join(threads[i], NULL);
		nxmm_check(r, ret == 0, __LINE__);
		r->checks += workers[i].result.checks;
		r->failures += workers[i].result.failures;
		if (!r->line && workers[i].result.line) {
			r->line = workers[i].result.line;
		}
	}
	nxmm_check(r, created == NXMM_THREADS, __LINE__);
	nxmm_check(r, sem_destroy(&start) == 0, __LINE__);
}

static int nxmm_report(const char *name, struct nxmm_result_s *r)
{
	printf("NXMM CASE %s %s checks=%u failures=%u line=%u\n", name,
		r->failures == 0 && r->checks > 0 ? "PASS" : "FAIL",
		r->checks, r->failures, r->line);
	return r->failures == 0 && r->checks > 0;
}

int nuttx_mm_test_main(int argc, char *argv[])
{
	const char *version = mm_nuttx_backend_version();
	const char *revision = mm_nuttx_backend_revision();
	struct nxmm_result_s r = {0};
	unsigned passed = 0;
	unsigned failed = 0;

	(void)argv;
	if (argc != 1) {
		printf("Usage: nuttx_mm_test\n");
		return EXIT_FAILURE;
	}
	printf("NXMM BEGIN backend=%s revision=%s heaps=%d seed=0x12345678 steps=%d threads=%d\n",
		version ? version : "missing", revision ? revision : "missing",
		CONFIG_KMM_NHEAPS, NXMM_STEPS, NXMM_THREADS);
	if (!version || !revision || strcmp(version, NXMM_VERSION) || strcmp(revision, NXMM_REVISION)) {
		printf("NXMM RESULT FAIL passed=0 failed=1 reason=backend-identity\n");
		return EXIT_FAILURE;
	}

#define RUN_CASE(name, statement) do { \
	memset(&r, 0, sizeof(r)); \
	statement; \
	if (nxmm_report(name, &r)) passed++; else failed++; \
} while (0)
	RUN_CASE("public", nxmm_public(&r));
	RUN_CASE("failure-preserves-data", nxmm_failure(&r));
	RUN_CASE("multiheap", nxmm_multiheap(&r));
	RUN_CASE("fragmentation", nxmm_fragment(&r, 0x12345678));
	RUN_CASE("threaded", nxmm_threaded(&r));
#undef RUN_CASE
	printf("NXMM RESULT %s passed=%u failed=%u\n", failed == 0 ? "PASS" : "FAIL", passed, failed);
	return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
