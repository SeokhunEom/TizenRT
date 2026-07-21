#include <tinyara/config.h>

#include <assert.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

#include <tinyara/arch.h>

#include "mem_leak_checker_domain.h"

#define MLC_DOMAIN_ACQUIRE_ATTEMPTS 8
#define MLC_DOMAIN_ACQUIRE_MAX_USEC 10000

static void mlc_domain_fatal(enum mlc_incomplete_reason_e reason, void *arg)
{
	(void)reason;
	(void)arg;
	PANIC();
}

static int mlc_domain_budget_end_chunks(
		struct mlc_lifecycle_s *lifecycle, size_t chunks)
{
	size_t index;
	int ret = 0;

	for (index = 0; index < chunks; index++) {
		if (mlc_lifecycle_budget_chunk_end(lifecycle,
			up_mem_leak_monotonic_usec()) < 0) {
			ret = -ETIME;
		}
	}
	return ret;
}

static void mlc_domain_unpin(void *arg)
{
	struct mlc_domain_guard_s *guard = arg;

#ifdef CONFIG_APP_BINARY_SEPARATION
	size_t index;
	int ret;
	int end_ret;
	size_t started = 0;

	for (index = 0; index < guard->pin_count; index++) {
		if (mlc_lifecycle_budget_chunk_begin(guard->lifecycle,
			MLC_BUDGET_DOMAIN_UNPIN, 1, up_mem_leak_monotonic_usec()) < 0) {
			end_ret = mlc_domain_budget_end_chunks(guard->lifecycle, started);
			guard->release_error = -E2BIG;
			if (end_ret < 0) {
				guard->release_error = end_ret;
			}
			mlc_lifecycle_invoke_fatal(guard->lifecycle, mlc_domain_fatal, guard);
			return;
		}
		started++;
	}
	ret = mm_loadable_domain_unpin_all(guard->pins, guard->pin_count);

	if (ret < 0) {
		end_ret = mlc_domain_budget_end_chunks(guard->lifecycle, started);
		guard->release_error = end_ret < 0 ? end_ret : ret;
		mlc_lifecycle_invoke_fatal(guard->lifecycle, mlc_domain_fatal, guard);
		return;
	}
	for (index = 0; index < guard->pin_count; index++) {
		ret = mlc_budget_release_ownership_identity(
			&guard->lifecycle->counters, MLC_BUDGET_DOMAIN_PIN,
			MLC_BUDGET_DOMAIN_UNPIN, (uint64_t)(uintptr_t)&guard->pins[index]);
		if (ret < 0) {
			end_ret = mlc_domain_budget_end_chunks(guard->lifecycle, started);
			guard->release_error = end_ret < 0 ? end_ret : ret;
			mlc_lifecycle_invoke_fatal(guard->lifecycle, mlc_domain_fatal, guard);
			return;
		}
	}
	end_ret = mlc_domain_budget_end_chunks(guard->lifecycle, started);
	if (end_ret < 0) {
		guard->release_error = end_ret;
		mlc_lifecycle_invoke_fatal(guard->lifecycle, mlc_domain_fatal, guard);
		return;
	}
#endif
	guard->pin_count = 0;
}

static void mlc_domain_leave_critical(void *arg)
{
	struct mlc_domain_guard_s *guard = arg;

	leave_critical_section(guard->critical_flags);
	guard->critical_owned = false;
}

static void mlc_domain_release_heaps(void *arg)
{
	struct mlc_domain_guard_s *guard = arg;
	pid_t pid = getpid();
	struct mm_heap_s *heap;
	int end_ret;

	while (guard->locked_heaps > 0) {
		if (mlc_lifecycle_budget_chunk_begin(guard->lifecycle,
			MLC_BUDGET_HEAP_RELEASE_VALIDATE, 1,
			up_mem_leak_monotonic_usec()) < 0) {
			guard->release_error = -E2BIG;
			mlc_lifecycle_invoke_fatal(guard->lifecycle, mlc_domain_fatal, guard);
			return;
		}
		heap = guard->heaps[--guard->locked_heaps];

		if (heap->mm_holder != pid || heap->mm_counts_held != 1) {
			end_ret = mlc_lifecycle_budget_chunk_end(guard->lifecycle,
				up_mem_leak_monotonic_usec());
			guard->release_error = -EUCLEAN;
			if (end_ret < 0) {
				guard->release_error = end_ret;
			}
			mlc_lifecycle_invoke_fatal(guard->lifecycle,
				mlc_domain_fatal, guard);
			return;
		}
		mm_givesemaphore(heap);
		if (mlc_budget_release_ownership_identity(
			&guard->lifecycle->counters, MLC_BUDGET_HEAP_ACQUIRE,
			MLC_BUDGET_HEAP_RELEASE_VALIDATE,
			(uint64_t)(uintptr_t)heap) < 0) {
			end_ret = mlc_lifecycle_budget_chunk_end(guard->lifecycle,
				up_mem_leak_monotonic_usec());
			guard->release_error = -EUCLEAN;
			if (end_ret < 0) {
				guard->release_error = end_ret;
			}
			mlc_lifecycle_invoke_fatal(guard->lifecycle, mlc_domain_fatal, guard);
			return;
		}
		end_ret = mlc_lifecycle_budget_chunk_end(guard->lifecycle,
			up_mem_leak_monotonic_usec());
		if (end_ret < 0) {
			guard->release_error = end_ret;
			mlc_lifecycle_invoke_fatal(guard->lifecycle, mlc_domain_fatal, guard);
			return;
		}
	}
}

static void mlc_domain_insert_heap(struct mlc_domain_guard_s *guard,
		struct mm_heap_s *heap)
{
	size_t index = guard->heap_count;

	while (index > 0 && (uintptr_t)guard->heaps[index - 1] >
		(uintptr_t)heap) {
		guard->heaps[index] = guard->heaps[index - 1];
		index--;
	}
	if ((index > 0 && guard->heaps[index - 1] == heap) ||
		(index < guard->heap_count && guard->heaps[index] == heap)) {
		return;
	}
	guard->heaps[index] = heap;
	guard->heap_count++;
}

static int mlc_domain_guard_try(struct mlc_lifecycle_s *lifecycle,
		struct mlc_domain_guard_s *guard)
{
	size_t index;
	int ret;

#ifdef CONFIG_APP_BINARY_SEPARATION
	size_t started = 0;
	int end_ret;

	for (index = 0; index < MLC_DOMAIN_PIN_CAPACITY; index++) {
		uint64_t identity = (uint64_t)(uintptr_t)&guard->pins[index];

		ret = mlc_lifecycle_budget_chunk_begin(lifecycle,
			MLC_BUDGET_DOMAIN_PIN, 1, up_mem_leak_monotonic_usec());
		if (ret < 0) {
			int begin_ret = ret;

			end_ret = mlc_domain_budget_end_chunks(lifecycle, started);
			while (index > 0) {
				index--;
				ret = mlc_budget_return_reservation_identity(
					&lifecycle->counters, MLC_BUDGET_DOMAIN_PIN,
					MLC_BUDGET_DOMAIN_UNPIN,
					(uint64_t)(uintptr_t)&guard->pins[index]);
				if (ret < 0) {
					mlc_lifecycle_invoke_fatal(lifecycle, mlc_domain_fatal, guard);
					return ret;
				}
			}
			if (end_ret < 0) {
				mlc_lifecycle_invoke_fatal(lifecycle, mlc_domain_fatal, guard);
			}
			return begin_ret;
		}
		ret = mlc_budget_reserve_ownership_identity(&lifecycle->counters,
			MLC_BUDGET_DOMAIN_PIN, MLC_BUDGET_DOMAIN_UNPIN, identity);
		if (ret < 0) {
			end_ret = mlc_domain_budget_end_chunks(lifecycle, started + 1);
			while (index > 0) {
				index--;
				ret = mlc_budget_return_reservation_identity(
					&lifecycle->counters, MLC_BUDGET_DOMAIN_PIN,
					MLC_BUDGET_DOMAIN_UNPIN,
					(uint64_t)(uintptr_t)&guard->pins[index]);
				if (ret < 0) {
					mlc_lifecycle_invoke_fatal(lifecycle, mlc_domain_fatal, guard);
					return ret;
				}
			}
			if (end_ret < 0) {
				mlc_lifecycle_invoke_fatal(lifecycle, mlc_domain_fatal, guard);
			}
			return -E2BIG;
		}
		started++;
	}
	ret = mm_loadable_domain_try_pin_all(guard->pins,
		MLC_DOMAIN_PIN_CAPACITY, &guard->pin_count);
	if (ret < 0) {
		int pin_ret = ret;

		for (index = 0; index < MLC_DOMAIN_PIN_CAPACITY; index++) {
			ret = mlc_budget_return_reservation_identity(&lifecycle->counters,
				MLC_BUDGET_DOMAIN_PIN, MLC_BUDGET_DOMAIN_UNPIN,
				(uint64_t)(uintptr_t)&guard->pins[index]);
			if (ret < 0) {
				mlc_lifecycle_invoke_fatal(lifecycle, mlc_domain_fatal, guard);
				return ret;
			}
		}
		end_ret = mlc_domain_budget_end_chunks(lifecycle, started);
		if (end_ret < 0) {
			mlc_lifecycle_invoke_fatal(lifecycle, mlc_domain_fatal, guard);
			return end_ret;
		}
		return pin_ret;
	}
	for (index = 0; index < guard->pin_count; index++) {
		if (mlc_budget_commit_ownership_identity(&lifecycle->counters,
			MLC_BUDGET_DOMAIN_PIN,
			(uint64_t)(uintptr_t)&guard->pins[index]) < 0) {
			mlc_lifecycle_invoke_fatal(lifecycle, mlc_domain_fatal, guard);
			return -E2BIG;
		}
		if (mlc_lifecycle_budget_chunk_end(lifecycle,
			up_mem_leak_monotonic_usec()) < 0) {
			mlc_lifecycle_invoke_fatal(lifecycle, mlc_domain_fatal, guard);
			return -ETIME;
		}
	}
	for (; index < MLC_DOMAIN_PIN_CAPACITY; index++) {
		if (mlc_budget_return_reservation_identity(&lifecycle->counters,
			MLC_BUDGET_DOMAIN_PIN, MLC_BUDGET_DOMAIN_UNPIN,
			(uint64_t)(uintptr_t)&guard->pins[index]) < 0) {
			mlc_lifecycle_invoke_fatal(lifecycle, mlc_domain_fatal, guard);
			return -E2BIG;
		}
		if (mlc_lifecycle_budget_chunk_end(lifecycle,
			up_mem_leak_monotonic_usec()) < 0) {
			mlc_lifecycle_invoke_fatal(lifecycle, mlc_domain_fatal, guard);
			return -ETIME;
		}
	}
	guard->report_pin_count = guard->pin_count;
#endif
	ret = mlc_lifecycle_push(lifecycle, MLC_PHASE_DOMAIN,
		MLC_RESOURCE_DOMAIN, mlc_domain_unpin, guard);
	if (ret < 0) {
		mlc_domain_unpin(guard);
		return ret;
	}

	ret = irq_try_enter_critical_fresh(&guard->critical_flags);
	if (ret < 0) {
		return ret;
	}
	guard->critical_owned = true;
	ret = mlc_lifecycle_push(lifecycle, MLC_PHASE_CRITICAL,
		MLC_RESOURCE_CRITICAL, mlc_domain_leave_critical, guard);
	if (ret < 0) {
		mlc_domain_leave_critical(guard);
		return ret;
	}

	for (index = 0; index < CONFIG_KMM_NHEAPS; index++) {
		mlc_domain_insert_heap(guard, &kmm_get_baseheap()[index]);
	}
#ifdef CONFIG_APP_BINARY_SEPARATION
	for (index = 0; index < guard->report_pin_count; index++) {
		if (guard->pins[index].heap != NULL) {
			mlc_domain_insert_heap(guard, guard->pins[index].heap);
		}
	}
#endif
	ret = mlc_lifecycle_push(lifecycle, MLC_PHASE_HEAPS,
		MLC_RESOURCE_HEAP, mlc_domain_release_heaps, guard);
	if (ret < 0) {
		return ret;
	}

	for (index = 0; index < guard->heap_count; index++) {
		if (mlc_lifecycle_budget_chunk_begin(lifecycle,
			MLC_BUDGET_HEAP_ACQUIRE, 1, up_mem_leak_monotonic_usec()) < 0) {
			return -E2BIG;
		}
		if (mlc_budget_reserve_ownership_identity(&lifecycle->counters,
			MLC_BUDGET_HEAP_ACQUIRE, MLC_BUDGET_HEAP_RELEASE_VALIDATE,
			(uint64_t)(uintptr_t)guard->heaps[index]) < 0) {
			if (mlc_lifecycle_budget_chunk_end(lifecycle,
				up_mem_leak_monotonic_usec()) < 0) {
				mlc_lifecycle_invoke_fatal(lifecycle, mlc_domain_fatal, guard);
			}
			return -E2BIG;
		}
		ret = mm_trysemaphore_fresh(guard->heaps[index]);
		if (ret < 0) {
			int acquire_ret = ret;
			int release_ret = mlc_budget_return_reservation_identity(
				&lifecycle->counters,
				MLC_BUDGET_HEAP_ACQUIRE, MLC_BUDGET_HEAP_RELEASE_VALIDATE,
				(uint64_t)(uintptr_t)guard->heaps[index]);
			if (release_ret < 0) {
				mlc_lifecycle_invoke_fatal(lifecycle, mlc_domain_fatal, guard);
				return release_ret;
			}
			if (mlc_lifecycle_budget_chunk_end(lifecycle,
				up_mem_leak_monotonic_usec()) < 0) {
				return -ETIME;
			}
			return acquire_ret;
		}
		if (mlc_budget_commit_ownership_identity(&lifecycle->counters,
			MLC_BUDGET_HEAP_ACQUIRE,
			(uint64_t)(uintptr_t)guard->heaps[index]) < 0) {
			int release_ret;
			int end_ret;

			mm_givesemaphore(guard->heaps[index]);
			release_ret = mlc_budget_return_reservation_identity(
				&lifecycle->counters, MLC_BUDGET_HEAP_ACQUIRE,
				MLC_BUDGET_HEAP_RELEASE_VALIDATE,
				(uint64_t)(uintptr_t)guard->heaps[index]);
			end_ret = mlc_lifecycle_budget_chunk_end(lifecycle,
				up_mem_leak_monotonic_usec());
			if (release_ret < 0 || end_ret < 0) {
				mlc_lifecycle_invoke_fatal(lifecycle, mlc_domain_fatal, guard);
				return release_ret < 0 ? release_ret : end_ret;
			}
			return -E2BIG;
		}
		guard->locked_heaps++;
		if (mlc_lifecycle_budget_chunk_end(lifecycle,
			up_mem_leak_monotonic_usec()) < 0) {
			return -ETIME;
		}
	}
	ret = mlc_lifecycle_advance(lifecycle, MLC_PHASE_CAPTURED);
	if (ret < 0) {
		return ret;
	}
	/* Keep the legacy call shape documented; production uses the already
	 * validated invocation ledger through the shared-budget variant. */
	/* mlc_pause_owner_begin(&guard->pause_owner, lifecycle->epoch_usec); */
	ret = mlc_pause_owner_begin_with_budget(&guard->pause_owner,
		lifecycle->epoch_usec, &lifecycle->counters);
	if (ret < 0) {
		return ret;
	}
	ret = mlc_lifecycle_push(lifecycle, MLC_PHASE_PAUSED,
		MLC_RESOURCE_PAUSE, mlc_pause_owner_cleanup, &guard->pause_owner);
	if (ret < 0) {
		mlc_pause_owner_cleanup(&guard->pause_owner);
		return ret;
	}
	ret = mlc_lifecycle_advance(lifecycle, MLC_PHASE_ANALYSIS);
	if (ret < 0) {
		return ret;
	}
	return 0;
}

int mlc_domain_guard_acquire(struct mlc_lifecycle_s *lifecycle,
		struct mlc_domain_guard_s *guard)
{
	uint64_t now;
	uint64_t elapsed_usec;
	unsigned int attempt;
	unsigned int attempts = 0;
	unsigned int elapsed = 0;
	size_t mark;
	int ret = -EBUSY;

	if (lifecycle == NULL || guard == NULL) {
		return -EINVAL;
	}
	memset(guard, 0, sizeof(*guard));
	guard->lifecycle = lifecycle;
	mark = mlc_lifecycle_mark(lifecycle);
	if (lifecycle->epoch_usec == 0) {
		return -EINVAL;
	}

	for (attempt = 0; attempt < MLC_DOMAIN_ACQUIRE_ATTEMPTS; attempt++) {
		now = up_mem_leak_monotonic_usec();
		if (now == 0 || now < lifecycle->epoch_usec) {
			return -ETIMEDOUT;
		}
		elapsed_usec = now - lifecycle->epoch_usec;
		if (elapsed_usec >= MLC_DOMAIN_ACQUIRE_MAX_USEC) {
			break;
		}
		elapsed = (unsigned int)elapsed_usec;
		guard->elapsed_usec = elapsed;
		attempts++;
		guard->attempt_count = attempts;
		ret = mlc_domain_guard_try(lifecycle, guard);
		if (ret == 0) {
			return 0;
		}
		mlc_lifecycle_unwind_to(lifecycle, mark);
		if (guard->release_error < 0) {
			return guard->release_error;
		}
		if (ret != -EBUSY) {
			break;
		}
		memset(guard, 0, sizeof(*guard));
		guard->lifecycle = lifecycle;
		guard->attempt_count = attempts;
		guard->elapsed_usec = elapsed;
	}
	return ret;
}

int mlc_domain_guard_release(struct mlc_lifecycle_s *lifecycle,
		struct mlc_domain_guard_s *guard, size_t mark)
{
	if (lifecycle == NULL || guard == NULL) {
		return -EINVAL;
	}
	mlc_lifecycle_unwind_to(lifecycle, mark);
	if (guard->pause_owner.error < 0 && guard->release_error == 0) {
		guard->release_error = guard->pause_owner.error;
	}
	return guard->release_error;
}

#ifdef CONFIG_APP_BINARY_SEPARATION
const struct mm_loadable_domain_pin_s *mlc_domain_guard_find_pin(
		const struct mlc_domain_guard_s *guard, const char *name)
{
	size_t index;

	for (index = 0; index < guard->report_pin_count; index++) {
		if (strncmp(guard->pins[index].name, name,
			MM_LOADABLE_DOMAIN_NAME_MAX) == 0) {
			return &guard->pins[index];
		}
	}
	return NULL;
}
#endif
