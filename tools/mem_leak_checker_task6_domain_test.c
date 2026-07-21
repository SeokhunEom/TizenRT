#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <tinyara/mm/mm.h>

#include "mm_loadable_domain_internal.h"

static void *disable_domain(void *arg)
{
	int *descriptor = arg;
	assert(mm_loadable_domain_disable_and_wait(descriptor) == 0);
	return NULL;
}

static struct mm_loadable_domain_registration_s registration(
		unsigned int slot, struct mm_heap_s *heap, int *descriptor)
{
	struct mm_loadable_domain_registration_s value;

	memset(&value, 0, sizeof(value));
	value.slot = slot;
	value.heap = heap;
	value.descriptor = descriptor;
	value.descriptor_container = descriptor;
	value.descriptor_container_size = sizeof(*descriptor);
	value.name = "loadable";
	value.writable_count = 2;
	value.writable[0].start = 0x1000;
	value.writable[0].size = 64;
	value.writable[0].container = 0x1000;
	value.writable[0].container_size = 512;
	value.writable[1].start = 0x1100;
	value.writable[1].size = 32;
	value.writable[1].container = 0x1000;
	value.writable[1].container_size = 512;
	return value;
}

int main(int argc, char **argv)
{
	struct mm_loadable_domain_pin_s pins[MM_LOADABLE_DOMAIN_CAPACITY];
	struct mm_loadable_domain_registration_s domain;
	struct mm_heap_s heap;
	uint32_t previous_generation = 0;
	size_t count;
	int descriptor = 7;
	int iteration;
	int repeat = argc == 2 ? atoi(argv[1]) : 1;

	assert(argc <= 2 && repeat > 0);

	mm_loadable_domain_initialize();
	mm_seminitialize(&heap);

	domain = registration(1, &heap, &descriptor);
	assert(mm_loadable_domain_register(&domain) == 0);
	assert(mm_loadable_domain_activate(&descriptor) == 0);
	assert(mm_loadable_domain_try_pin_all(pins,
		MM_LOADABLE_DOMAIN_CAPACITY, &count) == 0);
	assert(count == 1);
	assert(mm_loadable_domain_lock(true) == 0);
	assert(mm_loadable_domain_unpin_all(pins, count) == 0);
	mm_loadable_domain_unlock();
	assert(mm_loadable_domain_disable_and_wait(&descriptor) == 0);
	assert(mm_loadable_domain_finish_unload(&descriptor) == 0);

	for (iteration = 0; iteration < repeat; iteration++) {
		pthread_t unloader;
		int attempts;

		domain = registration(1, &heap, &descriptor);
		assert(mm_loadable_domain_register(&domain) == 0);
		assert(mm_loadable_domain_activate(&descriptor) == 0);
		assert(mm_loadable_domain_try_pin_all(pins,
			MM_LOADABLE_DOMAIN_CAPACITY, &count) == 0);
		assert(count == 1 && pins[0].heap == &heap);
		assert(pins[0].descriptor == &descriptor);
		assert(pins[0].writable_count == 2);
		assert(pins[0].generation != 0 &&
			pins[0].generation != previous_generation);
		previous_generation = pins[0].generation;
		assert(pthread_create(&unloader, NULL, disable_domain, &descriptor) == 0);
		for (attempts = 0; attempts < 10000; attempts++) {
			struct mm_loadable_domain_pin_s probe[MM_LOADABLE_DOMAIN_CAPACITY];
			size_t probe_count = 0;
			int ret = mm_loadable_domain_try_pin_all(probe,
				MM_LOADABLE_DOMAIN_CAPACITY, &probe_count);

			if (ret == 0 && probe_count == 0) {
				break;
			}
			if (ret == 0) {
				assert(mm_loadable_domain_unpin_all(probe, probe_count) == 0);
			}
		}
		assert(attempts < 10000);
		assert(mm_loadable_domain_unpin_all(pins, count) == 0);
		assert(pthread_join(unloader, NULL) == 0);
		assert(mm_loadable_domain_finish_unload(&descriptor) == 0);
	}
	printf("MLC_TASK6_DOMAIN status=PASS requested=%d completed=%d interleavings=2 generation=true residue=0\n",
		repeat, repeat);
	return 0;
}
