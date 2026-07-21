#include <assert.h>
#include <errno.h>
#include <stdint.h>

#include "mem_leak_checker_budget.h"

static uint64_t clock_value;

uint64_t up_mem_leak_monotonic_usec(void)
{
	return clock_value;
}

static void test_counter_boundaries(void)
{
	struct mlc_budget_counters_s budget;
	enum mlc_budget_counter_e counter;

	assert(mlc_budget_counters_init(&budget) == 0);
	assert(budget.ledger_capacity ==
		budget.configured[MLC_BUDGET_REGISTRY_ENUM] +
		budget.configured[MLC_BUDGET_DOMAIN_PIN] +
		budget.configured[MLC_BUDGET_HEAP_ACQUIRE]);
	for (counter = 0; counter < MLC_BUDGET_COUNTER_COUNT; counter++) {
		budget.remaining[counter] = budget.configured[counter];
		assert(mlc_budget_counter_take(&budget, counter,
			MLC_BUDGET_CHUNK_MAX) == 0);
		budget.remaining[counter] = 1;
		assert(mlc_budget_counter_take(&budget, counter, 1) == 0);
		assert(mlc_budget_counter_take(&budget, counter, 1) == -E2BIG);
		assert(mlc_budget_counter_take(&budget, counter,
			MLC_BUDGET_CHUNK_MAX + 1) == -E2BIG);
		budget.remaining[counter] = budget.configured[counter] + 1;
		assert(mlc_budget_counter_take(&budget, counter, 1) == -EINVAL);
	}
}

static void test_configured_maxima(void)
{
	struct mlc_budget_counters_s budget;
	enum mlc_budget_counter_e counter;

	for (counter = 0; counter < MLC_BUDGET_COUNTER_COUNT; counter++) {
		size_t remaining;

		assert(mlc_budget_counters_init(&budget) == 0);
		remaining = budget.configured[counter];
		while (remaining > 0) {
			size_t operations = remaining > MLC_BUDGET_CHUNK_MAX ?
				MLC_BUDGET_CHUNK_MAX : remaining;

			assert(mlc_budget_counter_take(&budget, counter, operations) == 0);
			remaining -= operations;
		}
		assert(budget.remaining[counter] == 0);
		assert(mlc_budget_counter_take(&budget, counter, 1) == -E2BIG);
		budget.remaining[counter] = budget.configured[counter] + 1;
		assert(mlc_budget_counter_take(&budget, counter, 1) == -EINVAL);
	}
}

static void test_reservation_boundaries(void)
{
	struct mlc_budget_counters_s budget;

	assert(mlc_budget_counters_init(&budget) == 0);
	assert(mlc_budget_reserve_ownership(&budget, MLC_BUDGET_HEAP_ACQUIRE,
		MLC_BUDGET_HEAP_RELEASE_VALIDATE) == 0);
	assert(budget.ledger_available == budget.ledger_capacity - 1);
	assert(mlc_budget_reserve_ownership(&budget, MLC_BUDGET_DOMAIN_PIN,
		MLC_BUDGET_HEAP_RELEASE_VALIDATE) == -EINVAL);
	assert(mlc_budget_commit_ownership(&budget, MLC_BUDGET_HEAP_ACQUIRE) == 0);
	assert(budget.ledger_committed == 1);
	assert(mlc_budget_release_ownership(&budget, MLC_BUDGET_HEAP_ACQUIRE,
		MLC_BUDGET_HEAP_RELEASE_VALIDATE) == 0);
	assert(budget.ledger_committed == 0);
	assert(budget.ledger_available == budget.ledger_capacity);
	assert(mlc_budget_reserve_ownership(&budget, MLC_BUDGET_HEAP_ACQUIRE,
		MLC_BUDGET_HEAP_RELEASE_VALIDATE) == 0);
	assert(mlc_budget_return_reservation(&budget, MLC_BUDGET_HEAP_ACQUIRE,
		MLC_BUDGET_HEAP_RELEASE_VALIDATE) == 0);
}

static void test_identity_ledger(void)
{
	struct mlc_budget_counters_s budget;

	assert(mlc_budget_counters_init(&budget) == 0);
	budget.ledger_available++;
	assert(mlc_budget_reserve_ownership(&budget, MLC_BUDGET_DOMAIN_PIN,
		MLC_BUDGET_DOMAIN_UNPIN) == -EINVAL);
	assert(mlc_budget_counters_init(&budget) == 0);
	budget.reverse_domain_available--;
	assert(mlc_budget_reserve_ownership(&budget, MLC_BUDGET_DOMAIN_PIN,
		MLC_BUDGET_DOMAIN_UNPIN) == -EINVAL);
	assert(mlc_budget_counters_init(&budget) == 0);
	budget.ledger_capacity--;
	assert(mlc_budget_reserve_ownership(&budget, MLC_BUDGET_DOMAIN_PIN,
		MLC_BUDGET_DOMAIN_UNPIN) == -EINVAL);
	assert(mlc_budget_counters_init(&budget) == 0);
	assert(mlc_budget_reserve_ownership_identity(&budget,
		MLC_BUDGET_DOMAIN_PIN, MLC_BUDGET_DOMAIN_UNPIN, 0x11) == 0);
	assert(mlc_budget_reserve_ownership_identity(&budget,
		MLC_BUDGET_DOMAIN_PIN, MLC_BUDGET_DOMAIN_UNPIN, 0x11) == -EINVAL);
	assert(mlc_budget_reserve_ownership_identity(&budget,
		MLC_BUDGET_DOMAIN_PIN, MLC_BUDGET_DOMAIN_UNPIN, 0x22) == 0);
	assert(mlc_budget_commit_ownership_identity(&budget,
		MLC_BUDGET_DOMAIN_PIN, 0x11) == 0);
	assert(mlc_budget_commit_ownership_identity(&budget,
		MLC_BUDGET_DOMAIN_PIN, 0x22) == 0);
	assert(mlc_budget_release_ownership_identity(&budget,
		MLC_BUDGET_DOMAIN_PIN, MLC_BUDGET_DOMAIN_UNPIN, 0x33) == -EPERM);
	assert(mlc_budget_release_ownership_identity(&budget,
		MLC_BUDGET_DOMAIN_PIN, MLC_BUDGET_DOMAIN_UNPIN, 0x11) == 0);
	assert(mlc_budget_release_ownership_identity(&budget,
		MLC_BUDGET_DOMAIN_PIN, MLC_BUDGET_DOMAIN_UNPIN, 0x22) == 0);
	assert(budget.ledger_committed == 0);
	assert(budget.ledger_reserved[MLC_BUDGET_DOMAIN_PIN] == 0);
	assert(budget.ledger_available == budget.ledger_capacity);
}

static void test_bytes_regions_and_deadlines(void)
{
	struct mlc_budget_counters_s budget;
	struct mlc_budget_region_s regions[] = {{4096}, {8192}};
	size_t nodes;

	assert(mlc_budget_counters_init(&budget) == 0);
	assert(mlc_budget_add_requested_bytes(&budget,
		MLC_SNAPSHOT_REQUESTED_BYTES_MAX) == 0);
	assert(mlc_budget_add_requested_bytes(&budget, 1) == -E2BIG);
	assert(mlc_budget_derive_region_nodes(&budget, regions, 2, 256,
		&nodes) == 0);
	assert(nodes == 48);
	clock_value = 100;
	assert(mlc_budget_set_epoch(&budget, clock_value, 78000, 95000) == 0);
	assert(budget.work_deadline_usec == clock_value + 78000);
	assert(budget.resume_deadline_usec == clock_value + 95000);
	assert(clock_value + 80000 < budget.resume_deadline_usec);
	assert(mlc_budget_chunk_begin(&budget, MLC_BUDGET_POINTER_WINDOW, 1,
		clock_value + 77999) == 0);
	assert(mlc_budget_chunk_end(&budget, clock_value + 77999) == 0);
	assert(mlc_budget_chunk_begin(&budget, MLC_BUDGET_POINTER_WINDOW, 1,
		clock_value + 78000) == -ETIME);
}

int main(void)
{
	test_counter_boundaries();
	test_configured_maxima();
	test_reservation_boundaries();
	test_identity_ledger();
	test_bytes_regions_and_deadlines();
	return 0;
}
