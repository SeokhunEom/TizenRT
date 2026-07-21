#include <tinyara/config.h>

#include <errno.h>
#include <limits.h>
#include <string.h>

#if defined(CONFIG_APP_BINARY_SEPARATION) && defined(__KERNEL__)
#include "mm_loadable_domain_internal.h"

int mm_loadable_domain_try_pin_all(FAR struct mm_loadable_domain_pin_s *pins,
		size_t capacity, FAR size_t *count)
{
	size_t index;
	size_t pinned = 0;
	int ret;

	if (pins == NULL || count == NULL) {
		return -EINVAL;
	}
	*count = 0;
	ret = mm_loadable_domain_lock(false);
	if (ret < 0) {
		return ret;
	}

	for (index = 0; index < MM_LOADABLE_DOMAIN_CAPACITY; index++) {
		struct mm_loadable_domain_slot_s *slot = &g_loadable_domains[index];
		struct mm_loadable_domain_pin_s *pin;

		if (__atomic_load_n(&slot->state, __ATOMIC_SEQ_CST) !=
			MM_DOMAIN_ACTIVE) {
			continue;
		}
		if (pinned >= capacity ||
			__atomic_load_n(&slot->pins, __ATOMIC_SEQ_CST) == UINT32_MAX) {
			ret = -ENOSPC;
			goto rollback;
		}
		__atomic_add_fetch(&slot->pins, 1, __ATOMIC_SEQ_CST);
		pin = &pins[pinned++];
		memset(pin, 0, sizeof(*pin));
		pin->slot = index;
		pin->generation = __atomic_load_n(&slot->generation, __ATOMIC_ACQUIRE);
		pin->heap = slot->registration.heap;
		pin->descriptor = slot->registration.descriptor;
		pin->descriptor_container = slot->registration.descriptor_container;
		pin->descriptor_container_size =
			slot->registration.descriptor_container_size;
		strncpy(pin->name, slot->name, sizeof(pin->name) - 1);
		pin->text_start = slot->registration.text_start;
		pin->text_size = slot->registration.text_size;
		pin->writable_count = slot->registration.writable_count;
		memcpy(pin->writable, slot->registration.writable,
			pin->writable_count * sizeof(pin->writable[0]));
	}

	mm_loadable_domain_unlock();
	*count = pinned;
	return 0;

rollback:
	while (pinned > 0) {
		struct mm_loadable_domain_pin_s *pin = &pins[--pinned];
		__atomic_sub_fetch(&g_loadable_domains[pin->slot].pins, 1,
			__ATOMIC_SEQ_CST);
		memset(pin, 0, sizeof(*pin));
	}
	mm_loadable_domain_unlock();
	return ret;
}

int mm_loadable_domain_unpin_all(
		FAR const struct mm_loadable_domain_pin_s *pins, size_t count)
{
	size_t index;
	size_t prior;

	if (pins == NULL && count != 0) {
		return -EINVAL;
	}
	for (index = 0; index < count; index++) {
		const struct mm_loadable_domain_pin_s *pin = &pins[index];
		struct mm_loadable_domain_slot_s *slot;

		if (pin->slot >= MM_LOADABLE_DOMAIN_CAPACITY) {
			return -EINVAL;
		}
		for (prior = 0; prior < index; prior++) {
			if (pins[prior].slot == pin->slot) {
				return -EINVAL;
			}
		}
		slot = &g_loadable_domains[pin->slot];
		if (__atomic_load_n(&slot->generation, __ATOMIC_ACQUIRE) !=
			pin->generation ||
			__atomic_load_n(&slot->pins, __ATOMIC_SEQ_CST) == 0) {
			return -ESTALE;
		}
	}

	while (count > 0) {
		const struct mm_loadable_domain_pin_s *pin = &pins[--count];
		struct mm_loadable_domain_slot_s *slot;
		uint32_t remaining;

		slot = &g_loadable_domains[pin->slot];
		remaining = __atomic_sub_fetch(&slot->pins, 1, __ATOMIC_SEQ_CST);
		if (remaining == 0 &&
			__atomic_load_n(&slot->state, __ATOMIC_SEQ_CST) == MM_DOMAIN_DYING) {
			(void)sem_post(&slot->drained);
		}
	}
	return 0;
}

#endif
