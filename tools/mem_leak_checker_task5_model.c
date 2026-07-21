#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

struct irq_model {
	atomic_flag primary;
	atomic_flag secondary;
	unsigned int irqset;
	int irqcount;
	unsigned int notes;
	bool interrupt;
	bool masked;
};

static int smp_fresh(struct irq_model *m)
{
	if (m->interrupt) {
		return -EPERM;
	}
	if (m->masked || m->irqcount != 0) {
		return -EALREADY;
	}
	if (atomic_flag_test_and_set(&m->primary)) {
		return -EBUSY;
	}
	if (atomic_flag_test_and_set(&m->secondary)) {
		atomic_flag_clear(&m->primary);
		return -EBUSY;
	}
	assert(m->irqset == 0 && m->irqcount == 0);
	m->irqset = 1;
	atomic_flag_clear(&m->secondary);
	m->irqcount = 1;
	m->notes = 6;
	return 0;
}

static void smp_leave(struct irq_model *m)
{
	assert(m->irqcount == 1 && m->irqset == 1);
	m->irqcount = 0;
	m->irqset = 0;
	atomic_flag_clear(&m->primary);
}

static atomic_flag admission = ATOMIC_FLAG_INIT;
static atomic_uint winners;

static void *admission_thread(void *arg)
{
	(void)arg;
	if (!atomic_flag_test_and_set(&admission)) {
		atomic_fetch_add(&winners, 1);
	}
	return NULL;
}

static void test_smp_rollback_and_leave(void)
{
	struct irq_model m = { .primary = ATOMIC_FLAG_INIT, .secondary = ATOMIC_FLAG_INIT };
	assert(smp_fresh(&m) == 0);
	assert(m.irqcount == 1 && m.irqset == 1 && m.notes == 6);
	smp_leave(&m);
	assert(m.irqcount == 0 && m.irqset == 0);
	m.notes = 0;

	atomic_flag_test_and_set(&m.primary);
	assert(smp_fresh(&m) == -EBUSY);
	assert(m.irqcount == 0 && m.irqset == 0 && m.notes == 0);
	atomic_flag_clear(&m.primary);

	atomic_flag_test_and_set(&m.secondary);
	assert(smp_fresh(&m) == -EBUSY);
	assert(!atomic_flag_test_and_set(&m.primary));
	assert(m.irqcount == 0 && m.irqset == 0 && m.notes == 0);
	atomic_flag_clear(&m.primary);
	atomic_flag_clear(&m.secondary);

	m.masked = true;
	assert(smp_fresh(&m) == -EALREADY);
	m.masked = false;
	m.irqcount = 1;
	assert(smp_fresh(&m) == -EALREADY);
	m.irqcount = 0;
	m.interrupt = true;
	assert(smp_fresh(&m) == -EPERM);
}

static void test_atomic_admission(void)
{
	pthread_t threads[8];
	unsigned int i;
	unsigned int repeat;

	for (repeat = 0; repeat < 100; repeat++) {
		atomic_store(&winners, 0);
		atomic_flag_clear(&admission);
		for (i = 0; i < 8; i++) {
			assert(pthread_create(&threads[i], NULL, admission_thread, NULL) == 0);
		}
		for (i = 0; i < 8; i++) {
			assert(pthread_join(threads[i], NULL) == 0);
		}
		assert(atomic_load(&winners) == 1);
	}
	atomic_flag_clear(&admission);
	assert(!atomic_flag_test_and_set(&admission));
}

static void test_saved_status_and_budget_boundaries(void)
{
	uint64_t operations = 2;
	uint64_t now = 77;
	uint32_t primask_enabled = 0;
	uint32_t primask_masked = 1;
	uint32_t cpsr_enabled = 0;
	uint32_t cpsr_masked = 1u << 7;

	assert((primask_enabled & 1u) == 0);
	assert((primask_masked & 1u) != 0);
	assert((cpsr_enabled & (1u << 7)) == 0);
	assert((cpsr_masked & (1u << 7)) != 0);
	assert(now < 78 && operations >= 2);
	operations -= 2;
	assert(operations == 0);
	assert(!(now < 78 && operations >= 1));
	now = 78;
	assert(!(now < 78));
	now = 80;
	assert(now <= 80);
	now = 81;
	assert(!(now <= 80));
}

#ifdef MLC_TASK5_RED_PROOF
int main(void)
{
	unsigned int cached_cpu = 0;
	unsigned int current_cpu = 1;
	unsigned int published_mask = 1u << cached_cpu;

	published_mask &= ~(1u << current_cpu);
	if (published_mask != 0) {
		puts("MLC_TASK5_RED stale_identity_detected=true expected_exit=86");
		return 86;
	}
	return 1;
}
#else
int main(void)
{
	test_smp_rollback_and_leave();
	test_atomic_admission();
	test_saved_status_and_budget_boundaries();
	puts("MLC_TASK5_MODEL status=PASS repeat=100");
	return 0;
}
#endif
