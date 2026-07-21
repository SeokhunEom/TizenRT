#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <tinyara/mm/mm.h>

#include "mm_loadable_domain_internal.h"

struct common_descriptor_s {
	uintptr_t text_start;
	size_t text_size;
	uintptr_t data_start;
	size_t data_size;
	uintptr_t bss_start;
	size_t bss_size;
};

struct common_publication_s {
	struct common_descriptor_s *library;
	uint32_t *app_id;
	struct common_descriptor_s *load_info;
	uint32_t load_version;
	unsigned int state;
	bool attributes_ready;
};

struct unload_context_s {
	struct common_descriptor_s *descriptor;
	int result;
	bool done;
};

static struct common_publication_s *g_publication;

static bool publication_ready(void *descriptor)
{
	return g_publication->library == descriptor &&
		g_publication->app_id != NULL &&
		g_publication->load_info == descriptor &&
		g_publication->load_version == 17 && g_publication->state == 1 &&
		g_publication->attributes_ready;
}

int sem_init(sem_t *sem, int pshared, unsigned int value)
{
	(void)pshared;
	assert(pthread_mutex_init(&sem->lock, NULL) == 0);
	assert(pthread_cond_init(&sem->condition, NULL) == 0);
	sem->semcount = (int)value;
	sem->flags = 1;
	return 0;
}

int sem_wait(sem_t *sem)
{
	assert(pthread_mutex_lock(&sem->lock) == 0);
	while (sem->semcount == 0) {
		assert(pthread_cond_wait(&sem->condition, &sem->lock) == 0);
	}
	sem->semcount--;
	assert(pthread_mutex_unlock(&sem->lock) == 0);
	return 0;
}

int sem_trywait(sem_t *sem)
{
	assert(pthread_mutex_lock(&sem->lock) == 0);
	if (sem->semcount == 0) {
		assert(pthread_mutex_unlock(&sem->lock) == 0);
		errno = EAGAIN;
		return -1;
	}
	sem->semcount--;
	assert(pthread_mutex_unlock(&sem->lock) == 0);
	return 0;
}

int sem_post(sem_t *sem)
{
	assert(pthread_mutex_lock(&sem->lock) == 0);
	sem->semcount++;
	assert(pthread_cond_signal(&sem->condition) == 0);
	assert(pthread_mutex_unlock(&sem->lock) == 0);
	return 0;
}

static void assert_no_pins(void)
{
	struct mm_loadable_domain_pin_s pins[MM_LOADABLE_DOMAIN_CAPACITY];
	size_t count = MM_LOADABLE_DOMAIN_CAPACITY;

	assert(mm_loadable_domain_try_pin_all(pins,
		MM_LOADABLE_DOMAIN_CAPACITY, &count) == 0);
	assert(count == 0);
}

static void publish_registration(
		struct mm_loadable_domain_registration_s *registration,
		struct common_descriptor_s *descriptor, uint8_t *allocation)
{
	memset(registration, 0, sizeof(*registration));
	registration->slot = 0;
	registration->descriptor = descriptor;
	registration->descriptor_container = descriptor;
	registration->descriptor_container_size = sizeof(*descriptor);
	registration->name = "common";
	registration->ready = publication_ready;
	registration->text_start = descriptor->text_start;
	registration->text_size = descriptor->text_size;
	registration->writable_count = 2;
	registration->writable[0].start = descriptor->data_start;
	registration->writable[0].size = descriptor->data_size;
	registration->writable[1].start = descriptor->bss_start;
	registration->writable[1].size = descriptor->bss_size;
	for (size_t index = 0; index < registration->writable_count; index++) {
		registration->writable[index].container = (uintptr_t)allocation;
		registration->writable[index].container_size = 512;
	}
}

static void *disable_and_wait(void *argument)
{
	struct unload_context_s *context = argument;

	context->result = mm_loadable_domain_disable_and_wait(context->descriptor);
	__atomic_store_n(&context->done, true, __ATOMIC_RELEASE);
	return NULL;
}

int main(void)
{
	uint8_t allocation[512];
	uint32_t app_id = 0;
	struct common_descriptor_s descriptor;
	struct common_publication_s publication;
	struct mm_loadable_domain_registration_s registration;
	struct mm_loadable_domain_pin_s pins[MM_LOADABLE_DOMAIN_CAPACITY];
	struct unload_context_s unload = { .descriptor = &descriptor };
	pthread_t unloader;
	size_t count;
	int attempts;

	memset(&descriptor, 0, sizeof(descriptor));
	memset(&publication, 0, sizeof(publication));
	g_publication = &publication;
	mm_loadable_domain_initialize();

	descriptor.text_start = 0x8000;
	assert_no_pins();
	descriptor.text_size = 256;
	assert_no_pins();
	descriptor.data_start = (uintptr_t)&allocation[64];
	assert_no_pins();
	descriptor.data_size = 96;
	assert_no_pins();
	descriptor.bss_start = (uintptr_t)&allocation[192];
	assert_no_pins();
	descriptor.bss_size = 80;
	assert_no_pins();

	publish_registration(&registration, &descriptor, allocation);
	assert(mm_loadable_domain_register(&registration) == 0);
	assert_no_pins();
	assert(mm_loadable_domain_activate(&descriptor) == -EAGAIN);
	publication.library = &descriptor;
	assert_no_pins();
	assert(mm_loadable_domain_activate(&descriptor) == -EAGAIN);
	publication.app_id = &app_id;
	assert_no_pins();
	assert(mm_loadable_domain_activate(&descriptor) == -EAGAIN);
	publication.load_info = &descriptor;
	assert_no_pins();
	assert(mm_loadable_domain_activate(&descriptor) == -EAGAIN);
	publication.load_version = 17;
	assert_no_pins();
	assert(mm_loadable_domain_activate(&descriptor) == -EAGAIN);
	publication.state = 1;
	assert_no_pins();
	assert(mm_loadable_domain_activate(&descriptor) == -EAGAIN);
	publication.attributes_ready = true;
	assert_no_pins();

	assert(mm_loadable_domain_activate(&descriptor) == 0);
	assert(mm_loadable_domain_try_pin_all(pins,
		MM_LOADABLE_DOMAIN_CAPACITY, &count) == 0);
	assert(count == 1);
	assert(pins[0].descriptor == &descriptor);
	assert(strcmp(pins[0].name, "common") == 0);
	assert(pins[0].text_start == descriptor.text_start);
	assert(pins[0].text_size == descriptor.text_size);
	assert(pins[0].writable_count == 2);
	for (size_t index = 0; index < pins[0].writable_count; index++) {
		assert(pins[0].writable[index].container == (uintptr_t)allocation);
		assert(pins[0].writable[index].container_size == sizeof(allocation));
	}
	assert(publication.library == &descriptor && publication.app_id == &app_id);
	assert(publication.load_info == &descriptor && publication.load_version == 17);
	assert(publication.state == 1 && publication.attributes_ready);

	assert(pthread_create(&unloader, NULL, disable_and_wait, &unload) == 0);
	for (attempts = 0; attempts < 100000; attempts++) {
		if (__atomic_load_n(&g_loadable_domains[0].state, __ATOMIC_ACQUIRE) ==
			MM_DOMAIN_DYING) {
			break;
		}
		sched_yield();
	}
	assert(attempts < 100000);
	assert(!__atomic_load_n(&unload.done, __ATOMIC_ACQUIRE));
	assert(mm_loadable_domain_unpin_all(pins, count) == 0);
	assert(pthread_join(unloader, NULL) == 0);
	assert(unload.result == 0 && __atomic_load_n(&unload.done, __ATOMIC_ACQUIRE));
	assert(mm_loadable_domain_finish_unload(&descriptor) == 0);
	assert_no_pins();

	printf("MLC_TASK6_PUBLISH status=PASS preactive_pins=0 active_pins=1 "
		"container=true handoff=true residue=0\n");
	return 0;
}
