#ifndef __KERNEL_DEBUG_MEM_LEAK_CHECKER_DOMAIN_H
#define __KERNEL_DEBUG_MEM_LEAK_CHECKER_DOMAIN_H

#include <tinyara/config.h>
#include <tinyara/irq.h>
#include <tinyara/mm/mm.h>

#include "mem_leak_checker_lifecycle.h"
#include "mem_leak_checker_pause_owner.h"

#ifdef CONFIG_APP_BINARY_SEPARATION
#define MLC_DOMAIN_PIN_CAPACITY MM_LOADABLE_DOMAIN_CAPACITY
#else
#define MLC_DOMAIN_PIN_CAPACITY 1
#endif

#define MLC_DOMAIN_HEAP_CAPACITY (CONFIG_KMM_NHEAPS + MLC_DOMAIN_PIN_CAPACITY)

struct mlc_domain_guard_s {
#ifdef CONFIG_APP_BINARY_SEPARATION
	struct mm_loadable_domain_pin_s pins[MLC_DOMAIN_PIN_CAPACITY];
#endif
	struct mm_heap_s *heaps[MLC_DOMAIN_HEAP_CAPACITY];
	size_t pin_count;
	size_t report_pin_count;
	size_t heap_count;
	size_t locked_heaps;
	irqstate_t critical_flags;
	bool critical_owned;
	int release_error;
	struct mlc_lifecycle_s *lifecycle;
	unsigned int attempt_count;
	unsigned int elapsed_usec;
	struct mlc_pause_owner_s pause_owner;
};

int mlc_domain_guard_acquire(struct mlc_lifecycle_s *lifecycle,
		struct mlc_domain_guard_s *guard);
int mlc_domain_guard_release(struct mlc_lifecycle_s *lifecycle,
		struct mlc_domain_guard_s *guard, size_t mark);
#ifdef CONFIG_APP_BINARY_SEPARATION
const struct mm_loadable_domain_pin_s *mlc_domain_guard_find_pin(
		const struct mlc_domain_guard_s *guard, const char *name);
#endif

#endif
