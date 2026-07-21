#include <errno.h>
#include <limits.h>
#include <string.h>

#include "mem_leak_checker_budget.h"

static struct mlc_budget_counters_s *volatile g_budget;

uint64_t mlc_budget_clock_now(void)
{
	return up_mem_leak_monotonic_usec();
}

struct mlc_budget_ledger_entry_s {
	uint64_t identity;
	enum mlc_budget_counter_e forward;
	enum mlc_budget_counter_e reverse;
	bool reserved;
	bool committed;
	uint32_t next_active;
	uint32_t prev_active;
	uint32_t next_free;
};

/* The checker is a singleton while it owns the protected heaps.  Keeping the
 * ledger out of the lifecycle stack lets the configured 65536-entry maxima be
 * enforced without making every invocation carry a multi-megabyte frame. */
static struct mlc_budget_ledger_entry_s
	g_ledger[3][MLC_BUDGET_COUNTER_MAX];

#define MLC_LEDGER_NONE UINT32_MAX

static uint32_t g_active_head[3];
static uint32_t g_active_tail[3];
static uint32_t g_free_head[3];
static uint32_t g_free_count[3];

void mlc_budget_bind(struct mlc_budget_counters_s *budget)
{
	__atomic_store_n(&g_budget, budget, __ATOMIC_RELEASE);
}

struct mlc_budget_counters_s *mlc_budget_current(void)
{
	return __atomic_load_n(&g_budget, __ATOMIC_ACQUIRE);
}

static int validate_max(uint32_t value)
{
	return value == 0 || value > MLC_BUDGET_COUNTER_MAX ? -ERANGE : 0;
}

static int checked_add_u64(uint64_t left, uint64_t right, uint64_t *result)
{
	if (result == NULL || UINT64_MAX - left < right) {
		return -EOVERFLOW;
	}
	*result = left + right;
	return 0;
}

static int checked_add_size(size_t left, size_t right, size_t *result)
{
	if (result == NULL || SIZE_MAX - left < right) {
		return -EOVERFLOW;
	}
	*result = left + right;
	return 0;
}

static int validate_counter(enum mlc_budget_counter_e counter)
{
	return counter < 0 || counter >= MLC_BUDGET_COUNTER_COUNT ? -EINVAL : 0;
}

static uint32_t configured_value(enum mlc_budget_counter_e counter)
{
	switch (counter) {
	case MLC_BUDGET_REGISTRY_ENUM: return CONFIG_MEM_LEAK_CHECKER_REGISTRY_ENUM_MAX;
	case MLC_BUDGET_DOMAIN_PIN: return CONFIG_MEM_LEAK_CHECKER_DOMAIN_PIN_MAX;
	case MLC_BUDGET_ROOT_CONTAINER_ENUM: return CONFIG_MEM_LEAK_CHECKER_ROOT_CONTAINER_ENUM_MAX;
	case MLC_BUDGET_HEAP_ACQUIRE: return CONFIG_MEM_LEAK_CHECKER_HEAP_ACQUIRE_MAX;
	case MLC_BUDGET_HEAP_RELEASE_VALIDATE: return CONFIG_MEM_LEAK_CHECKER_HEAP_RELEASE_VALIDATE_MAX;
	case MLC_BUDGET_DOMAIN_UNPIN: return CONFIG_MEM_LEAK_CHECKER_DOMAIN_UNPIN_MAX;
	case MLC_BUDGET_REGISTRY_UNWIND: return CONFIG_MEM_LEAK_CHECKER_REGISTRY_UNWIND_MAX;
	case MLC_BUDGET_PAUSE_ACK: return CONFIG_MEM_LEAK_CHECKER_PAUSE_ACK_MAX;
	case MLC_BUDGET_REMOTE_PAUSED_SERVICE: return CONFIG_MEM_LEAK_CHECKER_REMOTE_PAUSED_SERVICE_MAX;
	case MLC_BUDGET_CANCEL_COMPLETION: return CONFIG_MEM_LEAK_CHECKER_CANCEL_COMPLETION_MAX;
	case MLC_BUDGET_RESUME_COMPLETION: return CONFIG_MEM_LEAK_CHECKER_RESUME_COMPLETION_MAX;
	case MLC_BUDGET_SGI_DRAIN: return CONFIG_MEM_LEAK_CHECKER_SGI_DRAIN_MAX;
	default: return MLC_BUDGET_COUNTER_MAX;
	}
}

static int ownership_pair_valid(enum mlc_budget_counter_e forward,
		enum mlc_budget_counter_e reverse)
{
	return (forward == MLC_BUDGET_HEAP_ACQUIRE &&
		reverse == MLC_BUDGET_HEAP_RELEASE_VALIDATE) ||
		(forward == MLC_BUDGET_DOMAIN_PIN &&
			reverse == MLC_BUDGET_DOMAIN_UNPIN) ||
		(forward == MLC_BUDGET_REGISTRY_ENUM &&
			reverse == MLC_BUDGET_REGISTRY_UNWIND);
}

static size_t *reverse_available_for(struct mlc_budget_counters_s *budget,
		enum mlc_budget_counter_e reverse)
{
	switch (reverse) {
	case MLC_BUDGET_HEAP_RELEASE_VALIDATE:
		return &budget->reverse_heap_available;
	case MLC_BUDGET_DOMAIN_UNPIN:
		return &budget->reverse_domain_available;
	case MLC_BUDGET_REGISTRY_UNWIND:
		return &budget->reverse_registry_available;
	default:
		return NULL;
	}
}

static size_t reverse_capacity(const struct mlc_budget_counters_s *budget,
		enum mlc_budget_counter_e reverse)
{
	switch (reverse) {
	case MLC_BUDGET_HEAP_RELEASE_VALIDATE:
		return budget->configured[MLC_BUDGET_HEAP_RELEASE_VALIDATE];
	case MLC_BUDGET_DOMAIN_UNPIN:
		return budget->configured[MLC_BUDGET_DOMAIN_UNPIN];
	case MLC_BUDGET_REGISTRY_UNWIND:
		return budget->configured[MLC_BUDGET_REGISTRY_UNWIND];
	default:
		return 0;
	}
}

static int ownership_kind(enum mlc_budget_counter_e forward);

static int ledger_counters_valid(const struct mlc_budget_counters_s *budget)
{
	static const enum mlc_budget_counter_e forward_kinds[] = {
		MLC_BUDGET_REGISTRY_ENUM, MLC_BUDGET_DOMAIN_PIN,
		MLC_BUDGET_HEAP_ACQUIRE
	};
	size_t reserved = 0;
	size_t committed = 0;
	size_t capacity = 0;
	size_t active_reserved[sizeof(forward_kinds) /
		sizeof(forward_kinds[0])] = {0};
	size_t active_committed[sizeof(forward_kinds) /
		sizeof(forward_kinds[0])] = {0};
	size_t index;
	size_t kind;

	if (budget == NULL || budget->ledger_capacity == 0 ||
		budget->ledger_available > budget->ledger_capacity ||
		budget->ledger_committed > budget->ledger_capacity) {
		return -EINVAL;
	}
	for (kind = 0; kind < sizeof(g_ledger) / sizeof(g_ledger[0]); kind++) {
		uint32_t slot = g_active_head[kind];
		uint32_t previous = MLC_LEDGER_NONE;
		size_t active = 0;

		while (slot != MLC_LEDGER_NONE) {
			const struct mlc_budget_ledger_entry_s *entry;

			if (slot >= MLC_BUDGET_COUNTER_MAX || active++ >=
				MLC_BUDGET_COUNTER_MAX) {
				return -EINVAL;
			}
			entry = &g_ledger[kind][slot];
			if (entry->prev_active != previous ||
				(entry->reserved == entry->committed) ||
				ownership_kind(entry->forward) != (int)kind ||
				!ownership_pair_valid(entry->forward, entry->reverse)) {
				return -EINVAL;
			}
			if (entry->reserved) {
				active_reserved[kind]++;
			} else {
				active_committed[kind]++;
			}
			previous = slot;
			slot = entry->next_active;
		}
		if ((active == 0 && g_active_tail[kind] != MLC_LEDGER_NONE) ||
			(active != 0 && g_active_tail[kind] != previous)) {
			return -EINVAL;
		}
		if ((g_free_head[kind] != MLC_LEDGER_NONE &&
			g_free_head[kind] >= MLC_BUDGET_COUNTER_MAX) ||
			active > MLC_BUDGET_COUNTER_MAX - g_free_count[kind] ||
			active + g_free_count[kind] != MLC_BUDGET_COUNTER_MAX) {
			return -EINVAL;
		}
	}
	for (index = 0; index < MLC_BUDGET_COUNTER_COUNT; index++) {
		if (ownership_kind((enum mlc_budget_counter_e)index) < 0 &&
			(budget->ledger_reserved[index] != 0 ||
			 budget->ledger_committed_by_kind[index] != 0)) {
			return -EINVAL;
		}
	}
	for (index = 0; index < sizeof(forward_kinds) /
		sizeof(forward_kinds[0]); index++) {
		enum mlc_budget_counter_e forward = forward_kinds[index];
		enum mlc_budget_counter_e reverse =
			forward == MLC_BUDGET_REGISTRY_ENUM ? MLC_BUDGET_REGISTRY_UNWIND :
			forward == MLC_BUDGET_DOMAIN_PIN ? MLC_BUDGET_DOMAIN_UNPIN :
			MLC_BUDGET_HEAP_RELEASE_VALIDATE;
		size_t *reverse_available = reverse_available_for(
			(struct mlc_budget_counters_s *)budget, reverse);

		if (budget->ledger_reserved[forward] > budget->configured[forward] ||
			budget->ledger_committed_by_kind[forward] >
			budget->configured[forward] || reverse_available == NULL ||
			*reverse_available > reverse_capacity(budget, reverse)) {
			return -EINVAL;
		}
		if (checked_add_size(budget->ledger_reserved[forward],
			budget->ledger_committed_by_kind[forward], &capacity) < 0 ||
			capacity > reverse_capacity(budget, reverse) ||
			*reverse_available != reverse_capacity(budget, reverse) - capacity) {
			return -EINVAL;
		}
		if (active_reserved[index] != budget->ledger_reserved[forward] ||
			active_committed[index] != budget->ledger_committed_by_kind[forward]) {
			return -EINVAL;
		}
		if (checked_add_size(reserved, budget->ledger_reserved[forward],
			&reserved) < 0 || checked_add_size(committed,
			budget->ledger_committed_by_kind[forward], &committed) < 0) {
			return -EOVERFLOW;
		}
	}
	capacity = 0;
	for (index = 0; index < sizeof(forward_kinds) /
		sizeof(forward_kinds[0]); index++) {
		if (checked_add_size(capacity,
			budget->configured[forward_kinds[index]], &capacity) < 0) {
			return -EOVERFLOW;
		}
	}
	if (committed != budget->ledger_committed ||
		budget->ledger_capacity != capacity ||
		reserved > budget->ledger_capacity ||
		committed > budget->ledger_capacity - reserved ||
		budget->ledger_available != budget->ledger_capacity - reserved - committed) {
		return -EINVAL;
	}
	return 0;
}

int mlc_budget_counters_init(struct mlc_budget_counters_s *budget)
{
	enum mlc_budget_counter_e counter;
	size_t kind;
	size_t slot;
	size_t ledger_capacity = 0;

	if (budget == NULL) {
		return -EINVAL;
	}
	memset(budget, 0, sizeof(*budget));
	memset(g_ledger, 0, sizeof(g_ledger));
	for (kind = 0; kind < sizeof(g_ledger) / sizeof(g_ledger[0]); kind++) {
		g_active_head[kind] = MLC_LEDGER_NONE;
		g_active_tail[kind] = MLC_LEDGER_NONE;
		g_free_head[kind] = 0;
		g_free_count[kind] = MLC_BUDGET_COUNTER_MAX;
		for (slot = 0; slot < MLC_BUDGET_COUNTER_MAX; slot++) {
			struct mlc_budget_ledger_entry_s *entry = &g_ledger[kind][slot];

			entry->forward = MLC_BUDGET_COUNTER_COUNT;
			entry->reverse = MLC_BUDGET_COUNTER_COUNT;
			entry->next_active = MLC_LEDGER_NONE;
			entry->prev_active = MLC_LEDGER_NONE;
			entry->next_free = slot + 1 < MLC_BUDGET_COUNTER_MAX ?
				(uint32_t)(slot + 1) : MLC_LEDGER_NONE;
		}
	}
	for (counter = 0; counter < MLC_BUDGET_COUNTER_COUNT; counter++) {
		uint32_t value = configured_value(counter);

		if (validate_max(value) < 0) {
			return -ERANGE;
		}
		budget->configured[counter] = value;
		budget->remaining[counter] = value;
	}
	if (budget->configured[MLC_BUDGET_HEAP_RELEASE_VALIDATE] <
		budget->configured[MLC_BUDGET_HEAP_ACQUIRE] ||
		budget->configured[MLC_BUDGET_DOMAIN_UNPIN] <
		budget->configured[MLC_BUDGET_DOMAIN_PIN] ||
		budget->configured[MLC_BUDGET_REGISTRY_UNWIND] <
		budget->configured[MLC_BUDGET_REGISTRY_ENUM]) {
		return -ERANGE;
	}
	if (checked_add_size(ledger_capacity,
		budget->configured[MLC_BUDGET_REGISTRY_ENUM], &ledger_capacity) < 0 ||
		checked_add_size(ledger_capacity,
		budget->configured[MLC_BUDGET_DOMAIN_PIN], &ledger_capacity) < 0 ||
		checked_add_size(ledger_capacity,
		budget->configured[MLC_BUDGET_HEAP_ACQUIRE], &ledger_capacity) < 0) {
		return -EOVERFLOW;
	}
	budget->ledger_capacity = ledger_capacity;
	budget->ledger_available = ledger_capacity;
	budget->reverse_heap_available =
		budget->configured[MLC_BUDGET_HEAP_RELEASE_VALIDATE];
	budget->reverse_domain_available =
		budget->configured[MLC_BUDGET_DOMAIN_UNPIN];
	budget->reverse_registry_available =
		budget->configured[MLC_BUDGET_REGISTRY_UNWIND];
	return 0;
}

int mlc_budget_counter_take(struct mlc_budget_counters_s *budget,
		enum mlc_budget_counter_e counter, size_t operations)
{
	if (budget == NULL || validate_counter(counter) < 0) {
		return -EINVAL;
	}
	if (operations == 0 ||
		operations > MLC_BUDGET_CHUNK_MAX ||
		budget->remaining[counter] > budget->configured[counter] ||
		operations > budget->remaining[counter]) {
		return budget->remaining[counter] > budget->configured[counter] ?
			-EINVAL : -E2BIG;
	}
	budget->remaining[counter] -= (uint32_t)operations;
	return 0;
}

int mlc_budget_chunk_begin(struct mlc_budget_counters_s *budget,
		enum mlc_budget_counter_e counter, size_t operations,
		uint64_t now_usec)
{
	if (budget == NULL || (budget->epoch_valid &&
		(now_usec < budget->epoch_usec || now_usec >= budget->work_deadline_usec))) {
		return -ETIME;
	}
	return mlc_budget_counter_take(budget, counter, operations);
}

int mlc_budget_chunk_begin_resume(struct mlc_budget_counters_s *budget,
		enum mlc_budget_counter_e counter, size_t operations,
		uint64_t now_usec)
{
	if (budget == NULL || (budget->epoch_valid &&
		(now_usec < budget->epoch_usec || now_usec >=
			budget->resume_deadline_usec))) {
		return -ETIME;
	}
	return mlc_budget_counter_take(budget, counter, operations);
}

int mlc_budget_chunk_end(const struct mlc_budget_counters_s *budget,
		uint64_t now_usec)
{
	if (budget == NULL || (budget->epoch_valid &&
		(now_usec < budget->epoch_usec || now_usec >= budget->work_deadline_usec))) {
		return -ETIME;
	}
	return 0;
}

int mlc_budget_chunk_end_resume(const struct mlc_budget_counters_s *budget,
		uint64_t now_usec)
{
	if (budget == NULL || (budget->epoch_valid &&
		(now_usec < budget->epoch_usec || now_usec >=
			budget->resume_deadline_usec))) {
		return -ETIME;
	}
	return 0;
}

int mlc_budget_set_epoch(struct mlc_budget_counters_s *budget,
		uint64_t epoch_usec, uint64_t work_window_usec,
		uint64_t resume_window_usec)
{
	uint64_t work_deadline;
	uint64_t resume_deadline;

	if (budget == NULL || epoch_usec == 0 || work_window_usec > resume_window_usec) {
		return -EINVAL;
	}
	if (checked_add_u64(epoch_usec, work_window_usec, &work_deadline) < 0 ||
		checked_add_u64(epoch_usec, resume_window_usec, &resume_deadline) < 0) {
		return -EOVERFLOW;
	}
	budget->epoch_usec = epoch_usec;
	budget->work_deadline_usec = work_deadline;
	budget->resume_deadline_usec = resume_deadline;
	budget->epoch_valid = true;
	return 0;
}

int mlc_budget_add_requested_bytes(struct mlc_budget_counters_s *budget,
		size_t requested_bytes)
{
	uint64_t total;

	if (budget == NULL || checked_add_u64(budget->requested_payload_bytes,
		requested_bytes, &total) < 0 || total > MLC_SNAPSHOT_REQUESTED_BYTES_MAX) {
		return -E2BIG;
	}
	budget->requested_payload_bytes = total;
	return 0;
}

int mlc_budget_derive_region_nodes(struct mlc_budget_counters_s *budget,
		const struct mlc_budget_region_s *regions, size_t region_count,
		size_t min_chunk, size_t *node_ceiling)
{
	size_t index;
	size_t total = 0;

	if (budget == NULL || regions == NULL || node_ceiling == NULL ||
		region_count == 0 || min_chunk == 0) {
		return -EINVAL;
	}
	for (index = 0; index < region_count; index++) {
		size_t nodes = regions[index].bytes / min_chunk;

		if (mlc_budget_chunk_begin(budget, MLC_BUDGET_HEAP_REGION, 1,
			mlc_budget_clock_now()) < 0 ||
			mlc_budget_chunk_end(budget, mlc_budget_clock_now()) < 0 ||
			mlc_budget_chunk_begin(budget, MLC_BUDGET_REGION_BYTES, 1,
			mlc_budget_clock_now()) < 0 ||
			mlc_budget_chunk_end(budget, mlc_budget_clock_now()) < 0 ||
			total > SIZE_MAX - nodes) {
			return -E2BIG;
		}
		total += nodes;
	}
	if (total == 0 || total > budget->configured[MLC_BUDGET_HEAP_NODE]) {
		return -E2BIG;
	}
	*node_ceiling = total;
	return 0;
}

static int reservation_pair(struct mlc_budget_counters_s *budget,
		enum mlc_budget_counter_e forward, enum mlc_budget_counter_e reverse,
		size_t *reverse_available)
{
	if (validate_counter(forward) < 0 || validate_counter(reverse) < 0 ||
		ledger_counters_valid(budget) < 0 ||
		budget->ledger_available == 0 || *reverse_available == 0) {
		return -E2BIG;
	}
	budget->ledger_available--;
	(*reverse_available)--;
	return 0;
}

static int ownership_kind(enum mlc_budget_counter_e forward)
{
	switch (forward) {
	case MLC_BUDGET_REGISTRY_ENUM:
		return 0;
	case MLC_BUDGET_DOMAIN_PIN:
		return 1;
	case MLC_BUDGET_HEAP_ACQUIRE:
		return 2;
	default:
		return -EINVAL;
	}
}

static struct mlc_budget_ledger_entry_s *ledger_find(
		enum mlc_budget_counter_e forward, enum mlc_budget_counter_e reverse,
		uint64_t identity, bool reserved, bool committed)
{
	int kind = ownership_kind(forward);
	uint32_t slot;

	if (kind < 0) {
		return NULL;
	}
	slot = g_active_head[kind];
	while (slot != MLC_LEDGER_NONE) {
		struct mlc_budget_ledger_entry_s *entry = &g_ledger[kind][slot];

		if (entry->forward == forward && entry->reverse == reverse &&
			entry->identity == identity && entry->reserved == reserved &&
			entry->committed == committed) {
			return entry;
		}
		slot = entry->next_active;
	}
	return NULL;
}

static struct mlc_budget_ledger_entry_s *ledger_free(
		enum mlc_budget_counter_e forward)
{
	int kind = ownership_kind(forward);
	uint32_t slot;

	if (kind < 0) {
		return NULL;
	}
	slot = g_free_head[kind];
	if (slot == MLC_LEDGER_NONE || slot >= MLC_BUDGET_COUNTER_MAX) {
		return NULL;
	}
	if (g_ledger[kind][slot].reserved || g_ledger[kind][slot].committed ||
		g_ledger[kind][slot].next_active != MLC_LEDGER_NONE ||
		g_ledger[kind][slot].prev_active != MLC_LEDGER_NONE) {
		return NULL;
	}
	return &g_ledger[kind][slot];
}

static void ledger_activate(struct mlc_budget_ledger_entry_s *entry)
{
	int kind = ownership_kind(entry->forward);
	uint32_t slot = (uint32_t)(entry - &g_ledger[kind][0]);
	uint32_t tail = g_active_tail[kind];

	g_free_head[kind] = entry->next_free;
	g_free_count[kind]--;
	entry->next_free = MLC_LEDGER_NONE;
	entry->prev_active = tail;
	entry->next_active = MLC_LEDGER_NONE;
	if (tail == MLC_LEDGER_NONE) {
		g_active_head[kind] = slot;
	} else {
		g_ledger[kind][tail].next_active = slot;
	}
	g_active_tail[kind] = slot;
}

static void ledger_deactivate(struct mlc_budget_ledger_entry_s *entry)
{
	int kind = ownership_kind(entry->forward);
	uint32_t slot = (uint32_t)(entry - &g_ledger[kind][0]);

	if (entry->prev_active == MLC_LEDGER_NONE) {
		g_active_head[kind] = entry->next_active;
	} else {
		g_ledger[kind][entry->prev_active].next_active = entry->next_active;
	}
	if (entry->next_active == MLC_LEDGER_NONE) {
		g_active_tail[kind] = entry->prev_active;
	} else {
		g_ledger[kind][entry->next_active].prev_active = entry->prev_active;
	}
	entry->reserved = false;
	entry->committed = false;
	entry->forward = MLC_BUDGET_COUNTER_COUNT;
	entry->reverse = MLC_BUDGET_COUNTER_COUNT;
	entry->next_active = MLC_LEDGER_NONE;
	entry->prev_active = MLC_LEDGER_NONE;
	entry->next_free = g_free_head[kind];
	g_free_head[kind] = slot;
	g_free_count[kind]++;
}

int mlc_budget_reserve_ownership_identity(struct mlc_budget_counters_s *budget,
		enum mlc_budget_counter_e forward, enum mlc_budget_counter_e reverse,
		uint64_t identity)
{
	size_t *reverse_available;
	struct mlc_budget_ledger_entry_s *entry;

	if (budget == NULL || validate_counter(forward) < 0 ||
		validate_counter(reverse) < 0 || !ownership_pair_valid(forward, reverse)) {
		return -EINVAL;
	}
	if (ledger_counters_valid(budget) < 0) {
		return -EINVAL;
	}
	if (budget->ledger_reserved[forward] >=
		budget->configured[forward] ||
		budget->ledger_committed_by_kind[forward] >
		budget->configured[forward] - budget->ledger_reserved[forward]) {
		return -E2BIG;
	}
	reverse_available = reverse_available_for(budget, reverse);
	if (ledger_find(forward, reverse, identity, true, false) != NULL ||
		ledger_find(forward, reverse, identity, false, true) != NULL) {
		return -EINVAL;
	}
	entry = ledger_free(forward);
	if (reverse_available == NULL || entry == NULL ||
		reservation_pair(budget, forward, reverse, reverse_available) < 0) {
		return -E2BIG;
	}
	entry->identity = identity;
	entry->forward = forward;
	entry->reverse = reverse;
	entry->reserved = true;
	ledger_activate(entry);
	budget->ledger_reserved[forward]++;
	return 0;
}

int mlc_budget_reserve_ownership(struct mlc_budget_counters_s *budget,
		enum mlc_budget_counter_e forward, enum mlc_budget_counter_e reverse)
{
	size_t *reverse_available;

	if (budget == NULL || validate_counter(forward) < 0 ||
		validate_counter(reverse) < 0) {
		return -EINVAL;
	}
	if (!ownership_pair_valid(forward, reverse)) {
		return -EINVAL;
	}
	reverse_available = reverse_available_for(budget, reverse);
	if (reverse_available == NULL) {
		return -EINVAL;
	}
	return mlc_budget_reserve_ownership_identity(budget, forward, reverse, 0);
}

int mlc_budget_commit_ownership_identity(struct mlc_budget_counters_s *budget,
		enum mlc_budget_counter_e forward, uint64_t identity)
{
	struct mlc_budget_ledger_entry_s *entry;

	if (budget == NULL || validate_counter(forward) < 0) {
		return -EINVAL;
	}
	if (ledger_counters_valid(budget) < 0) {
		return -EINVAL;
	}
	entry = ledger_find(forward,
		forward == MLC_BUDGET_HEAP_ACQUIRE ? MLC_BUDGET_HEAP_RELEASE_VALIDATE :
		forward == MLC_BUDGET_DOMAIN_PIN ? MLC_BUDGET_DOMAIN_UNPIN :
		MLC_BUDGET_REGISTRY_UNWIND, identity, true, false);
	if (entry == NULL || budget->ledger_reserved[forward] == 0) {
		return -EPERM;
	}
	if (budget->ledger_committed == SIZE_MAX ||
		budget->ledger_committed_by_kind[forward] == UINT32_MAX) {
		return -EOVERFLOW;
	}
	entry->reserved = false;
	entry->committed = true;
	budget->ledger_reserved[forward]--;
	budget->ledger_committed_by_kind[forward]++;
	budget->ledger_committed++;
	return 0;
}

int mlc_budget_commit_ownership(struct mlc_budget_counters_s *budget,
		enum mlc_budget_counter_e forward)
{
	return mlc_budget_commit_ownership_identity(budget, forward, 0);
}

int mlc_budget_return_reservation(struct mlc_budget_counters_s *budget,
		enum mlc_budget_counter_e forward, enum mlc_budget_counter_e reverse)
{
	if (budget == NULL || validate_counter(forward) < 0 ||
		validate_counter(reverse) < 0 || !ownership_pair_valid(forward, reverse) ||
		budget->ledger_available >= budget->ledger_capacity) {
		return -EINVAL;
	}
	if (ledger_counters_valid(budget) < 0) {
		return -EINVAL;
	}
	{
		size_t *reverse_available = reverse_available_for(budget, reverse);

		if (reverse_available == NULL ||
			*reverse_available >= reverse_capacity(budget, reverse)) {
			return -EINVAL;
		}
		struct mlc_budget_ledger_entry_s *entry = ledger_find(forward, reverse,
			0, true, false);
		if (entry == NULL || budget->ledger_reserved[forward] == 0) {
			return -EPERM;
		}
		ledger_deactivate(entry);
		budget->ledger_reserved[forward]--;
		budget->ledger_available++;
		(*reverse_available)++;
	}
	return 0;
}

int mlc_budget_return_reservation_identity(struct mlc_budget_counters_s *budget,
		enum mlc_budget_counter_e forward, enum mlc_budget_counter_e reverse,
		uint64_t identity)
{
	size_t *reverse_available;
	struct mlc_budget_ledger_entry_s *entry;

	if (budget == NULL || validate_counter(forward) < 0 ||
		validate_counter(reverse) < 0 || !ownership_pair_valid(forward, reverse)) {
		return -EINVAL;
	}
	if (ledger_counters_valid(budget) < 0) {
		return -EINVAL;
	}
	reverse_available = reverse_available_for(budget, reverse);
	entry = ledger_find(forward, reverse, identity, true, false);
	if (reverse_available == NULL || entry == NULL ||
		budget->ledger_available >= budget->ledger_capacity ||
		*reverse_available >= reverse_capacity(budget, reverse) ||
		budget->ledger_reserved[forward] == 0) {
		return -EPERM;
	}
	ledger_deactivate(entry);
	budget->ledger_reserved[forward]--;
	budget->ledger_available++;
	(*reverse_available)++;
	return 0;
}

int mlc_budget_release_ownership_identity(struct mlc_budget_counters_s *budget,
		enum mlc_budget_counter_e forward, enum mlc_budget_counter_e reverse,
		uint64_t identity)
{
	size_t *reverse_available;
	struct mlc_budget_ledger_entry_s *entry;

	if (budget == NULL || validate_counter(forward) < 0 ||
		validate_counter(reverse) < 0 || !ownership_pair_valid(forward, reverse)) {
		return -EINVAL;
	}
	if (ledger_counters_valid(budget) < 0) {
		return -EINVAL;
	}
	reverse_available = reverse_available_for(budget, reverse);
	entry = ledger_find(forward, reverse, identity, false, true);
	if (reverse_available == NULL || entry == NULL ||
		budget->ledger_committed == 0 ||
		budget->ledger_committed_by_kind[forward] == 0 ||
		budget->ledger_available >= budget->ledger_capacity ||
		*reverse_available >= reverse_capacity(budget, reverse)) {
		return -EPERM;
	}
	ledger_deactivate(entry);
	budget->ledger_committed--;
	budget->ledger_committed_by_kind[forward]--;
	budget->ledger_available++;
	(*reverse_available)++;
	return 0;
}

int mlc_budget_release_ownership(struct mlc_budget_counters_s *budget,
		enum mlc_budget_counter_e forward, enum mlc_budget_counter_e reverse)
{
	return mlc_budget_release_ownership_identity(budget, forward, reverse, 0);
}
