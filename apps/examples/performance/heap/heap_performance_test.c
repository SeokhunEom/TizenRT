/****************************************************************************
 *
 * Copyright 2019 Samsung Electronics All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
 * either express or implied. See the License for the specific
 * language governing permissions and limitations under the License.
 *
 ****************************************************************************/

#include <tinyara/config.h>
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

#define HEAP_MAX_WORKERS 8
#define HEAP_MAX_SAMPLES 31
#define HEAP_MAX_BLOCKS 256
#define HEAP_STACKSIZE 6144
#define HEAP_NS_PER_SEC ((uint64_t)1000000000)

struct heap_options_s {
	const char *mode;
	int workers;
	int blocks;
	int repeat;
	int samples;
	int warmup;
	int interval;
	int min_size;
	int max_size;
	int priority;
	int priority_step;
	int policy;
	bool spread;
	bool mixed;
};

struct heap_sample_s {
	uint64_t alloc_ns;
	uint64_t free_ns;
	uint64_t begin;
	uint64_t end;
	uint64_t epoch;
	int zero_alloc;
	int zero_free;
	int error;
	int cycle;
	int block;
};

struct heap_run_s;
struct heap_worker_s {
	struct heap_run_s *run;
	pthread_t thread;
	sem_t gate;
	void **data;
	int cpu;
	int priority;
	bool stop;
	struct heap_sample_s result;
	struct heap_sample_s samples[HEAP_MAX_SAMPLES];
};

struct heap_run_s {
	struct heap_options_s options;
	sem_t ready;
	sem_t done;
	int size;
	int created;
	int gates;
	struct heap_worker_s workers[HEAP_MAX_WORKERS];
};

static void heap_usage(void)
{
	printf("Usage: heaptest [INTERVAL_SECONDS REPEAT]\n"
		"       heaptest [--mode MODE] [--workers N] [options]\n"
		"Modes: single, samecpu, smp, samecpu-prio, smp-prio\n"
		"Options (defaults):\n"
		"  --blocks 1..256 (100): allocations per batch, then free all\n"
		"  --repeat 1..100000 (100): batches per worker per sample\n"
		"  --samples 1..31 (5), --warmup 0..10 (1): rounds per size\n"
		"  --interval 0..3600 (1): seconds BETWEEN sizes, excluded\n"
		"  --min-size N (16), --max-size N (8192): powers of two, 16..1048576\n"
		"  --workers 1..8 (1 for single, otherwise 2)\n"
		"  --priority N (100), --priority-step N (10): ascending prio ladder\n"
		"  --policy fifo|rr (rr when configured, otherwise fifo)\n"
		"  --help: print help without running\n"
		"SMP modes require >=2 CPUs. Samecpu modes pin workers to CPU0.\n"
		"Each successful worker sample makes blocks*repeat malloc AND free calls.\n");
}

static int heap_number(const char *text, int min, int max, int *out)
{
	unsigned int value = 0;
	const char *p;

	if (*text == '\0') {
		return EINVAL;
	}
	for (p = text; *p; p++) {
		unsigned int digit;
		if (*p < '0' || *p > '9') {
			return EINVAL;
		}
		digit = *p - '0';
		if (value > (unsigned int)max / 10 ||
			(value == (unsigned int)max / 10 && digit > (unsigned int)max % 10)) {
			return EINVAL;
		}
		value = value * 10 + digit;
	}
	if (value < (unsigned int)min) {
		return EINVAL;
	}
	*out = (int)value;
	return 0;
}

static int heap_parse(int argc, char **argv, struct heap_options_s *o)
{
	int i;
	int ret;
	int min_priority = sched_get_priority_min(SCHED_FIFO);
	int max_priority = sched_get_priority_max(SCHED_FIFO);
	bool workers_set = false;

	memset(o, 0, sizeof(*o));
	o->mode = "single";
	o->workers = 1;
	o->blocks = 100;
	o->repeat = 100;
	o->samples = 5;
	o->warmup = 1;
	o->interval = 1;
	o->min_size = 16;
	o->max_size = 8192;
	o->priority = 100;
	o->priority_step = 10;
#if CONFIG_RR_INTERVAL > 0
	o->policy = SCHED_RR;
#else
	o->policy = SCHED_FIFO;
#endif
	if (min_priority < 0 || max_priority < min_priority) {
		return EINVAL;
	}
	if (argc == 3 && argv[1][0] != '-') {
		ret = heap_number(argv[1], 0, 3600, &o->interval);
		return ret ? ret : heap_number(argv[2], 1, 100000, &o->repeat);
	}
	for (i = 1; i < argc; i += 2) {
		const char *key = argv[i];
		const char *value;
		int *dest = NULL;
		int min = 1;
		int max = 0;

		if (i + 1 >= argc) {
			return EINVAL;
		}
		value = argv[i + 1];
		if (!strcmp(key, "--mode")) {
			o->mode = value;
			continue;
		} else if (!strcmp(key, "--policy")) {
			if (!strcmp(value, "fifo")) {
				o->policy = SCHED_FIFO;
			} else if (!strcmp(value, "rr")) {
#if CONFIG_RR_INTERVAL > 0
				o->policy = SCHED_RR;
#else
				return ENOTSUP;
#endif
			} else {
				return EINVAL;
			}
			continue;
		} else if (!strcmp(key, "--workers")) {
			dest = &o->workers;
			max = HEAP_MAX_WORKERS;
			workers_set = true;
		} else if (!strcmp(key, "--blocks")) {
			dest = &o->blocks;
			max = HEAP_MAX_BLOCKS;
		} else if (!strcmp(key, "--repeat")) {
			dest = &o->repeat;
			max = 100000;
		} else if (!strcmp(key, "--samples")) {
			dest = &o->samples;
			max = HEAP_MAX_SAMPLES;
		} else if (!strcmp(key, "--warmup")) {
			dest = &o->warmup;
			min = 0;
			max = 10;
		} else if (!strcmp(key, "--interval")) {
			dest = &o->interval;
			min = 0;
			max = 3600;
		} else if (!strcmp(key, "--min-size") || !strcmp(key, "--max-size")) {
			dest = !strcmp(key, "--min-size") ? &o->min_size : &o->max_size;
			min = 16;
			max = 1048576;
		} else if (!strcmp(key, "--priority")) {
			dest = &o->priority;
			min = min_priority;
			max = max_priority;
		} else if (!strcmp(key, "--priority-step")) {
			dest = &o->priority_step;
			max = max_priority - min_priority;
		} else {
			return EINVAL;
		}
		ret = heap_number(value, min, max, dest);
		if (ret) {
			return ret;
		}
	}

	o->spread = !strcmp(o->mode, "smp") || !strcmp(o->mode, "smp-prio");
	o->mixed = !strcmp(o->mode, "samecpu-prio") || !strcmp(o->mode, "smp-prio");
	if (strcmp(o->mode, "single") && strcmp(o->mode, "samecpu") && !o->spread && !o->mixed) {
		return EINVAL;
	}
	if (strcmp(o->mode, "single")) {
		if (!workers_set) {
			o->workers = 2;
		}
		if (o->workers < 2) {
			return EINVAL;
		}
	} else if (o->workers != 1) {
		return EINVAL;
	}
#if !defined(CONFIG_SMP) || CONFIG_SMP_NCPUS < 2
	if (o->spread) {
		return ENOTSUP;
	}
#endif
	if (o->min_size > o->max_size || (o->min_size & (o->min_size - 1)) ||
		(o->max_size & (o->max_size - 1)) || o->priority < min_priority ||
		o->priority > max_priority || (o->mixed &&
		o->priority_step > (max_priority - o->priority) / (o->workers - 1))) {
		return EINVAL;
	}
	return 0;
}

static int heap_now(uint64_t *ns)
{
	struct timespec ts;
	if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0) {
		return errno;
	}
	*ns = (uint64_t)ts.tv_sec * HEAP_NS_PER_SEC + ts.tv_nsec;
	return 0;
}

static int heap_wait(sem_t *sem)
{
	while (sem_wait(sem) < 0) {
		if (errno != EINTR) {
			return errno;
		}
	}
	return 0;
}

static int heap_sem_init(sem_t *sem)
{
	int error;
	if (sem_init(sem, 0, 0) < 0) {
		return errno;
	}
	if (sem_setprotocol(sem, SEM_PRIO_NONE) < 0) {
		error = errno;
		sem_destroy(sem);
		return error;
	}
	return 0;
}

static void heap_measure(struct heap_worker_s *w)
{
	struct heap_sample_s *r = &w->result;
	const struct heap_options_s *o = &w->run->options;
	uint64_t before;
	uint64_t allocated;
	uint64_t freed;
	int live = 0;
	int i;
	int j;

	memset(r, 0, sizeof(*r));
	for (i = 0; i < o->repeat; i++) {
		r->cycle = i;
		r->error = heap_now(&before);
		if (r->error) {
			break;
		}
		if (i == 0) {
			r->begin = before;
		} else if (before < r->end) {
			r->error = EIO;
			break;
		}
		for (j = 0; j < o->blocks; j++) {
			w->data[j] = malloc(w->run->size);
			if (w->data[j] == NULL) {
				r->error = ENOMEM;
				r->block = j;
				goto cleanup;
			}
			live++;
		}
		r->error = heap_now(&allocated);
		if (r->error) {
			break;
		}
		for (j = 0; j < o->blocks; j++) {
			free(w->data[j]);
		}
		live = 0;
		r->error = heap_now(&freed);
		if (r->error) {
			break;
		}
		if (allocated < before || freed < allocated) {
			r->error = EIO;
			break;
		}
		r->alloc_ns += allocated - before;
		r->free_ns += freed - allocated;
		r->zero_alloc += allocated == before;
		r->zero_free += freed == allocated;
		r->end = freed;
	}
cleanup:
	/* Failed samples are discarded. Cleanup is outside successful timing. */
	for (j = 0; j < live; j++) {
		free(w->data[j]);
	}
}

static void *heap_worker(void *arg)
{
	struct heap_worker_s *w = arg;
	int error;

	/* The coordinator owns worker lifetime, including failure cleanup. */
	error = pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
	w->result.error = error;
	sem_post(&w->run->ready);
	if (error) {
		return NULL;
	}
	for (;;) {
		error = heap_wait(&w->gate);
		if (error) {
			w->result.error = error;
			sem_post(&w->run->done);
			return NULL;
		}
		if (w->stop) {
			return NULL;
		}
		heap_measure(w);
		sem_post(&w->run->done);
	}
}

static int heap_start(struct heap_run_s *run)
{
	const struct heap_options_s *o = &run->options;
	int i;
	int ret;

	for (i = 0; i < o->workers; i++) {
		struct heap_worker_s *w = &run->workers[i];
		pthread_attr_t attr;
		struct sched_param param;

		w->run = run;
		w->priority = o->priority + (o->mixed ? i * o->priority_step : 0);
#ifdef CONFIG_SMP
		w->cpu = o->spread ? i % CONFIG_SMP_NCPUS : 0;
#endif
		w->data = calloc(o->blocks, sizeof(void *));
		if (!w->data) {
			return ENOMEM;
		}
		ret = heap_sem_init(&w->gate);
		if (ret) {
			return ret;
		}
		run->gates++;
		ret = pthread_attr_init(&attr);
		if (ret) {
			return ret;
		}
		memset(&param, 0, sizeof(param));
		param.sched_priority = w->priority;
		ret = pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
		if (!ret) {
			ret = pthread_attr_setschedpolicy(&attr, o->policy);
		}
		if (!ret) {
			ret = pthread_attr_setschedparam(&attr, &param);
		}
		if (!ret) {
			ret = pthread_attr_setstacksize(&attr, HEAP_STACKSIZE);
		}
#ifdef CONFIG_SMP
		if (!ret) {
			cpu_set_t set;
			CPU_ZERO(&set);
			CPU_SET(w->cpu, &set);
			ret = pthread_attr_setaffinity_np(&attr, sizeof(set), &set);
		}
#endif
		if (!ret) {
			ret = pthread_create(&w->thread, &attr, heap_worker, w);
			if (!ret) {
				run->created++;
			}
		}
		pthread_attr_destroy(&attr);
		if (ret) {
			return ret;
		}
	}
	for (i = 0; i < run->created; i++) {
		ret = heap_wait(&run->ready);
		if (ret) {
			return ret;
		}
	}
	for (i = 0; i < run->created; i++) {
		if (run->workers[i].result.error) {
			return run->workers[i].result.error;
		}
	}
	return 0;
}

static int heap_stop(struct heap_run_s *run)
{
	int i;
	int error = 0;
	int ret;

	for (i = 0; i < run->created; i++) {
		run->workers[i].stop = true;
		sem_post(&run->workers[i].gate);
	}
	for (i = 0; i < run->created; i++) {
		ret = pthread_join(run->workers[i].thread, NULL);
		if (ret && !error) {
			error = ret;
		}
	}
	/* Never free storage possibly still referenced by a failed join. */
	if (error) {
		return error;
	}
	for (i = 0; i < run->gates; i++) {
		sem_destroy(&run->workers[i].gate);
	}
	for (i = 0; i < run->options.workers; i++) {
		free(run->workers[i].data);
	}
	return 0;
}

static int heap_round(struct heap_run_s *run, uint64_t *epoch)
{
	int i;
	int error;
	int ret;

	/* A completion post precedes the next gate wait. Verify that all
	 * workers have actually blocked before releasing the next round;
	 * otherwise an already-running SMP worker could bypass launch setup.
	 * This polling/sleep is outside every measured interval.
	 */
	for (i = 0; i < run->created; i++) {
		int count;
		do {
			if (sem_getvalue(&run->workers[i].gate, &count) < 0) {
				return errno;
			}
			if (count >= 0) {
				usleep(1000);
			}
		} while (count >= 0);
	}

	/* Release all blocked workers before allowing any to preempt us.
	 * This is launch setup only, never a lock around allocator calls.
	 * The common epoch includes scheduler unlock/dispatch latency.
	 */
	sched_lock();
	error = heap_now(epoch);
	if (!error) {
		for (i = 0; i < run->created; i++) {
			sem_post(&run->workers[i].gate);
		}
	}
	sched_unlock();
	if (error) {
		return error;
	}
	for (i = 0; i < run->created; i++) {
		ret = heap_wait(&run->done);
		if (ret && !error) {
			error = ret;
		}
	}
	for (i = 0; i < run->created; i++) {
		struct heap_sample_s *r = &run->workers[i].result;
		r->epoch = *epoch;
		if (!r->error && (r->begin < *epoch || r->end < r->begin)) {
			r->error = EIO;
		}
		if (r->error) {
			printf("Worker %d failed: size=%d cycle=%d block=%d error=%d\n",
				i, run->size, r->cycle, r->block, r->error);
			if (!error) {
				error = r->error;
			}
		}
	}
	return error;
}

static int heap_compare(const void *a, const void *b)
{
	uint64_t x = *(const uint64_t *)a;
	uint64_t y = *(const uint64_t *)b;
	return x < y ? -1 : x > y;
}

static uint64_t heap_stats(const char *label, uint64_t *values, int count)
{
	uint64_t median;
	qsort(values, count, sizeof(*values), heap_compare);
	median = values[count / 2];
	if (count % 2 == 0) {
		median = values[count / 2 - 1] + (median - values[count / 2 - 1]) / 2;
	}
	printf("  %s ns min/median/max=%" PRIu64 "/%" PRIu64 "/%" PRIu64 "\n",
		label, values[0], median, values[count - 1]);
	return median;
}

static void heap_report(struct heap_run_s *run)
{
	const struct heap_options_s *o = &run->options;
	uint64_t values[HEAP_MAX_SAMPLES];
	uint64_t calls = (uint64_t)o->blocks * o->repeat;
	uint64_t median;
	int i;
	int s;

	printf("Size=%d bytes; calls/worker/sample: malloc=%" PRIu64 " free=%" PRIu64 "\n", run->size, calls, calls);
	for (i = 0; i < o->workers; i++) {
		struct heap_worker_s *w = &run->workers[i];
		int quantized = 0;
		printf("Worker=%d cpu=%d priority=%d\n", i, w->cpu, w->priority);
		for (s = 0; s < o->samples; s++) {
			values[s] = w->samples[s].alloc_ns;
			quantized += w->samples[s].zero_alloc + w->samples[s].zero_free;
		}
		median = heap_stats("malloc totals", values, o->samples);
		printf("  malloc amortized median=%" PRIu64 " ns/call\n", median / calls);
		for (s = 0; s < o->samples; s++) {
			values[s] = w->samples[s].free_ns;
		}
		median = heap_stats("free totals", values, o->samples);
		printf("  free amortized median=%" PRIu64 " ns/call\n", median / calls);
		for (s = 0; s < o->samples; s++) {
			values[s] = w->samples[s].end - w->samples[s].begin;
		}
		heap_stats("worker duration", values, o->samples);
		for (s = 0; s < o->samples; s++) {
			values[s] = w->samples[s].begin - w->samples[s].epoch;
		}
		heap_stats("launch delay", values, o->samples);
		for (s = 0; s < o->samples; s++) {
			values[s] = w->samples[s].end - w->samples[s].epoch;
		}
		heap_stats("completion latency", values, o->samples);
		if (quantized) {
			printf("  WARNING: %d batch phases below clock resolution; increase --blocks.\n", quantized);
		}
	}
	for (s = 0; s < o->samples; s++) {
		uint64_t end = 0;
		for (i = 0; i < o->workers; i++) {
			if (run->workers[i].samples[s].end > end) {
				end = run->workers[i].samples[s].end;
			}
		}
		values[s] = end - run->workers[0].samples[s].epoch;
	}
	median = heap_stats("aggregate makespan", values, o->samples);
	if (median) {
		printf("  aggregate=%" PRIu64 " allocator calls/s (malloc+free)\n",
			2 * calls * o->workers * HEAP_NS_PER_SEC / median);
	} else {
		printf("  aggregate=N/A (below clock resolution)\n");
	}
}

#ifdef CONFIG_BUILD_KERNEL
int main(int argc, char *argv[])
#else
int heaptest_main(int argc, char *argv[])
#endif
{
	struct heap_options_s options;
	struct heap_run_s *run;
	struct timespec resolution;
	uint64_t epoch;
	int oldstate;
	int error;
	int cleanup;
	int size;
	int round;
	int i;

	if (argc == 2 && !strcmp(argv[1], "--help")) {
		heap_usage();
		return EXIT_SUCCESS;
	}
	error = heap_parse(argc, argv, &options);
	if (error) {
		printf("Heap performance FAIL: invalid/unsupported arguments (error=%d).\n", error);
		heap_usage();
		return EXIT_FAILURE;
	}
	if (clock_getres(CLOCK_MONOTONIC, &resolution) < 0) {
		printf("Heap performance FAIL: clock_getres error=%d\n", errno);
		return EXIT_FAILURE;
	}
	error = pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldstate);
	if (error) {
		printf("Heap performance FAIL: cancel state error=%d\n", error);
		return EXIT_FAILURE;
	}
	run = calloc(1, sizeof(*run));
	if (!run) {
		error = ENOMEM;
		goto finish;
	}
	run->options = options;
	error = heap_sem_init(&run->ready);
	if (error) {
		free(run);
		goto finish;
	}
	error = heap_sem_init(&run->done);
	if (error) {
		sem_destroy(&run->ready);
		free(run);
		goto finish;
	}
	printf("Heap performance mode=%s workers=%d policy=%s blocks=%d repeat=%d samples=%d warmup=%d interval=%ds\n",
		options.mode, options.workers, options.policy == SCHED_FIFO ? "fifo" : "rr",
		options.blocks, options.repeat, options.samples, options.warmup, options.interval);
	printf("Clock=MONOTONIC resolution=%" PRIu64 " ns; max live payload=%" PRIu64 " bytes (metadata/stacks extra)\n",
		(uint64_t)resolution.tv_sec * HEAP_NS_PER_SEC + resolution.tv_nsec,
		(uint64_t)options.max_size * options.blocks * options.workers);
#ifdef CONFIG_ARCH_BOARD
	printf("Board=%s\n", CONFIG_ARCH_BOARD);
#endif
#ifdef CONFIG_SMP
	printf("CPUs=%d\n", CONFIG_SMP_NCPUS);
#else
	printf("CPUs=1\n");
#endif
#ifdef CONFIG_PRIORITY_INHERITANCE
	printf("Priority inheritance=configured\n");
#else
	printf("Priority inheritance=disabled\n");
#endif
	#ifdef CONFIG_DEBUG_MM_HEAPINFO
	printf("Heap allocation tracking=enabled\n");
#endif
#ifdef CONFIG_DEBUG_MM_FREEINFO
	printf("Heap free tracking=enabled\n");
#endif
#ifdef CONFIG_DEBUG_MM_UAF
	printf("Heap use-after-free checking=enabled\n");
#endif
	printf("Batch timing includes clock/loop, preemption and heap-lock costs; not individual-call worst case.\n");
	error = heap_start(run);
	if (!error) {
		for (size = options.min_size; size <= options.max_size && !error; size *= 2) {
			run->size = size;
			for (round = -options.warmup; round < options.samples; round++) {
				error = heap_round(run, &epoch);
				if (error) {
					break;
				}
				if (round >= 0) {
					for (i = 0; i < options.workers; i++) {
						run->workers[i].samples[round] = run->workers[i].result;
					}
				}
			}
			if (!error) {
				heap_report(run);
				if (size < options.max_size) {
					unsigned int remaining = options.interval;
					while (remaining) {
						remaining = sleep(remaining);
					}
				}
			}
		}
	}
	cleanup = heap_stop(run);
	if (!cleanup) {
		sem_destroy(&run->ready);
		sem_destroy(&run->done);
		free(run);
	} else if (!error) {
		error = cleanup;
	}
finish:
	printf("Heap performance %s (error=%d)\n", error ? "FAIL" : "PASS", error);
	pthread_setcancelstate(oldstate, NULL);
	return error ? EXIT_FAILURE : EXIT_SUCCESS;
}
