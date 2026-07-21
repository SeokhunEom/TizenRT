#include <tinyara/config.h>

#include <errno.h>
#include <limits.h>
#include <semaphore.h>
#include <string.h>
#include <tinyara/mm/mm.h>
#if defined(CONFIG_APP_BINARY_SEPARATION) && defined(__KERNEL__)
#include "mm_loadable_domain_internal.h"
static sem_t g_domain_lock;
static bool g_domain_initialized;
struct mm_loadable_domain_slot_s
	g_loadable_domains[MM_LOADABLE_DOMAIN_CAPACITY];

int mm_loadable_domain_lock(bool wait)
{
	int ret;
	if (!g_domain_initialized) {
		return -EAGAIN;
	}

	if (!wait) {
		ret = sem_trywait(&g_domain_lock);
		return ret == 0 ? 0 : (get_errno() == EAGAIN ? -EBUSY : -get_errno());
	}

	do {
		ret = sem_wait(&g_domain_lock);
	} while (ret != 0 && get_errno() == EINTR);
	return ret == 0 ? 0 : -get_errno();
}

void mm_loadable_domain_unlock(void)
{
	(void)sem_post(&g_domain_lock);
}

static struct mm_loadable_domain_slot_s *mm_domain_by_descriptor(
		FAR void *descriptor)
{
	size_t index;

	for (index = 0; index < MM_LOADABLE_DOMAIN_CAPACITY; index++) {
		if (__atomic_load_n(&g_loadable_domains[index].state,
			__ATOMIC_ACQUIRE) != MM_DOMAIN_EMPTY &&
			g_loadable_domains[index].registration.descriptor == descriptor) {
			return &g_loadable_domains[index];
		}
	}
	return NULL;
}

void mm_loadable_domain_initialize(void)
{
	size_t index;

	if (g_domain_initialized) {
		return;
	}
	memset(g_loadable_domains, 0, sizeof(g_loadable_domains));
	(void)sem_init(&g_domain_lock, 0, 1);
	for (index = 0; index < MM_LOADABLE_DOMAIN_CAPACITY; index++) {
		(void)sem_init(&g_loadable_domains[index].drained, 0, 0);
	}
	g_domain_initialized = true;
}

int mm_loadable_domain_register(
		FAR const struct mm_loadable_domain_registration_s *registration)
{
	struct mm_loadable_domain_slot_s *slot;
	uint32_t generation;
	size_t index;
	int ret;

	if (registration == NULL || registration->descriptor == NULL ||
		registration->slot >= MM_LOADABLE_DOMAIN_CAPACITY ||
		registration->writable_count > MM_LOADABLE_DOMAIN_WRITABLE_MAX ||
		registration->name == NULL) {
		return -EINVAL;
	}
	if (registration->descriptor_container == NULL ||
		registration->descriptor_container_size == 0 ||
		(uintptr_t)registration->descriptor_container > UINTPTR_MAX -
		registration->descriptor_container_size ||
		(uintptr_t)registration->descriptor <
		(uintptr_t)registration->descriptor_container ||
		(uintptr_t)registration->descriptor >=
		(uintptr_t)registration->descriptor_container +
		registration->descriptor_container_size ||
		(registration->text_size != 0 &&
		registration->text_start > UINTPTR_MAX - registration->text_size)) {
		return -EINVAL;
	}
	for (index = 0; index < registration->writable_count; index++) {
		const struct mm_loadable_mapping_s *mapping =
			&registration->writable[index];

		if (mapping->size == 0 || mapping->container_size == 0 ||
			mapping->start < mapping->container ||
			mapping->start > UINTPTR_MAX - mapping->size ||
			mapping->container > UINTPTR_MAX - mapping->container_size ||
			mapping->start + mapping->size >
			mapping->container + mapping->container_size) {
			return -EINVAL;
		}
	}

	ret = mm_loadable_domain_lock(true);
	if (ret < 0) {
		return ret;
	}

	slot = &g_loadable_domains[registration->slot];
	if (__atomic_load_n(&slot->state, __ATOMIC_ACQUIRE) != MM_DOMAIN_EMPTY ||
		__atomic_load_n(&slot->pins, __ATOMIC_ACQUIRE) != 0) {
		mm_loadable_domain_unlock();
		return -EBUSY;
	}

	generation = __atomic_load_n(&slot->generation, __ATOMIC_RELAXED) + 1;
	if (generation == 0) {
		mm_loadable_domain_unlock();
		return -EOVERFLOW;
	}

	__atomic_store_n(&slot->generation, generation, __ATOMIC_RELAXED);
	slot->registration = *registration;
	strncpy(slot->name, registration->name, sizeof(slot->name) - 1);
	slot->name[sizeof(slot->name) - 1] = '\0';
	slot->registration.name = slot->name;
	__atomic_store_n(&slot->state, MM_DOMAIN_PREPARING, __ATOMIC_RELEASE);
	mm_loadable_domain_unlock();
	return 0;
}

int mm_loadable_domain_activate(FAR void *descriptor)
{
	struct mm_loadable_domain_slot_s *slot;
	int ret = mm_loadable_domain_lock(true);

	if (ret < 0) {
		return ret;
	}
	slot = mm_domain_by_descriptor(descriptor);
	if (slot == NULL || __atomic_load_n(&slot->state, __ATOMIC_ACQUIRE) !=
		MM_DOMAIN_PREPARING) {
		mm_loadable_domain_unlock();
		return -EINVAL;
	}
	if (slot->registration.ready != NULL &&
		!slot->registration.ready(descriptor)) {
		mm_loadable_domain_unlock();
		return -EAGAIN;
	}
	__atomic_store_n(&slot->state, MM_DOMAIN_ACTIVE, __ATOMIC_RELEASE);
	mm_loadable_domain_unlock();
	return 0;
}

int mm_loadable_domain_abort(FAR void *descriptor)
{
	struct mm_loadable_domain_slot_s *slot;
	int ret = mm_loadable_domain_lock(true);

	if (ret < 0) {
		return ret;
	}
	slot = mm_domain_by_descriptor(descriptor);
	if (slot == NULL || __atomic_load_n(&slot->state, __ATOMIC_ACQUIRE) !=
		MM_DOMAIN_PREPARING) {
		mm_loadable_domain_unlock();
		return -EINVAL;
	}
	memset(&slot->registration, 0, sizeof(slot->registration));
	slot->name[0] = '\0';
	__atomic_store_n(&slot->state, MM_DOMAIN_EMPTY, __ATOMIC_RELEASE);
	mm_loadable_domain_unlock();
	return 0;
}

int mm_loadable_domain_disable_and_wait(FAR void *descriptor)
{
	struct mm_loadable_domain_slot_s *slot;
	bool wait;
	int ret = mm_loadable_domain_lock(true);

	if (ret < 0) {
		return ret;
	}
	slot = mm_domain_by_descriptor(descriptor);
	if (slot == NULL || __atomic_load_n(&slot->state, __ATOMIC_ACQUIRE) !=
		MM_DOMAIN_ACTIVE) {
		mm_loadable_domain_unlock();
		return -EINVAL;
	}
	__atomic_store_n(&slot->state, MM_DOMAIN_DYING, __ATOMIC_SEQ_CST);
	wait = __atomic_load_n(&slot->pins, __ATOMIC_SEQ_CST) != 0;
	mm_loadable_domain_unlock();

	while (wait && sem_wait(&slot->drained) != 0) {
		if (get_errno() != EINTR) {
			return -get_errno();
		}
	}
	return 0;
}

int mm_loadable_domain_reactivate(FAR void *descriptor)
{
	struct mm_loadable_domain_slot_s *slot;
	int ret = mm_loadable_domain_lock(true);

	if (ret < 0) {
		return ret;
	}
	slot = mm_domain_by_descriptor(descriptor);
	if (slot == NULL || __atomic_load_n(&slot->state, __ATOMIC_ACQUIRE) !=
		MM_DOMAIN_DYING ||
		__atomic_load_n(&slot->pins, __ATOMIC_ACQUIRE) != 0) {
		mm_loadable_domain_unlock();
		return -EINVAL;
	}
	__atomic_store_n(&slot->state, MM_DOMAIN_ACTIVE, __ATOMIC_RELEASE);
	mm_loadable_domain_unlock();
	return 0;
}

int mm_loadable_domain_finish_unload(FAR void *descriptor)
{
	struct mm_loadable_domain_slot_s *slot;
	int ret = mm_loadable_domain_lock(true);

	if (ret < 0) {
		return ret;
	}
	slot = mm_domain_by_descriptor(descriptor);
	if (slot == NULL || __atomic_load_n(&slot->state, __ATOMIC_ACQUIRE) !=
		MM_DOMAIN_DYING ||
		__atomic_load_n(&slot->pins, __ATOMIC_ACQUIRE) != 0) {
		mm_loadable_domain_unlock();
		return -EINVAL;
	}
	memset(&slot->registration, 0, sizeof(slot->registration));
	slot->name[0] = '\0';
	__atomic_store_n(&slot->state, MM_DOMAIN_EMPTY, __ATOMIC_RELEASE);
	mm_loadable_domain_unlock();
	return 0;
}
#endif
