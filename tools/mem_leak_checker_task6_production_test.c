#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <string.h>

#include <tinyara/binfmt/binfmt.h>
#include <tinyara/mm/mm.h>

static atomic_bool g_stop_updater;
static atomic_bool g_started_updater;

static void *update_app_id(void *unused)
{
	uint32_t value = 1;

	(void)unused;
	while (!atomic_load(&g_stop_updater)) {
		binfmt_update_umm_app_id(value++);
		atomic_store(&g_started_updater, true);
	}
	return NULL;
}

static void reset_binary(struct binary_s *bin)
{
	memset(bin, 0, sizeof(*bin));
	bin->binary_idx = 0;
	bin->bin_name = "xip-app";
	bin->uheap = (struct mm_heap_s *)(uintptr_t)0x1800;
	bin->sections[BIN_TEXT] = 0x8000;
	bin->sizes[BIN_TEXT] = 0x200;
	bin->ram_region_start = 0x1000;
	bin->ram_region_end = 0x2000;
	bin->sections[BIN_DATA] = 0x1100;
	bin->sizes[BIN_DATA] = 0x100;
	bin->sections[BIN_BSS] = 0x1400;
	bin->sizes[BIN_BSS] = 0x180;
}

static void test_xip_container(void)
{
	struct mm_loadable_domain_pin_s pin;
	struct binary_s bin;
	size_t count;

	reset_binary(&bin);
	assert(binfmt_register_app_domain(&bin) == 0);
	assert(mm_loadable_domain_activate(&bin) == 0);
	assert(mm_loadable_domain_try_pin_all(&pin, 1, &count) == 0);
	assert(count == 1 && pin.writable_count == 2);
	for (size_t index = 0; index < pin.writable_count; index++) {
		assert(pin.writable[index].container == 0x1000);
		assert(pin.writable[index].container_size == 0x1000);
	}
	assert(mm_loadable_domain_unpin_all(&pin, count) == 0);
	assert(mm_loadable_domain_disable_and_wait(&bin) == 0);
	assert(mm_loadable_domain_finish_unload(&bin) == 0);

	reset_binary(&bin);
	bin.sections[BIN_DATA] = 0x1f80;
	bin.sizes[BIN_DATA] = 0x100;
	assert(binfmt_register_app_domain(&bin) == -EINVAL);
	assert(mm_loadable_domain_abort(&bin) == -EINVAL);

	reset_binary(&bin);
	bin.sections[BIN_BSS] = 0x2000;
	bin.sizes[BIN_BSS] = 1;
	assert(binfmt_register_app_domain(&bin) == -EINVAL);
	assert(mm_loadable_domain_abort(&bin) == -EINVAL);

	reset_binary(&bin);
	bin.ram_region_end = bin.ram_region_start;
	assert(binfmt_register_app_domain(&bin) == -EINVAL);
	assert(mm_loadable_domain_abort(&bin) == -EINVAL);

	reset_binary(&bin);
	bin.ram_region_end = bin.ram_region_start - 1;
	assert(binfmt_register_app_domain(&bin) == -EINVAL);
	assert(mm_loadable_domain_abort(&bin) == -EINVAL);
}

static void test_common_identity_quiescence(void)
{
	pthread_t updater;
	uint32_t app_id = 0;
	uint32_t snapshot;

	assert(binfmt_exchange_umm_app_id(&app_id) == NULL);
	assert(pthread_create(&updater, NULL, update_app_id, NULL) == 0);
	while (!atomic_load(&g_started_updater)) {
	}
	assert(binfmt_exchange_umm_app_id(NULL) == &app_id);
	snapshot = app_id;
	for (volatile unsigned int index = 0; index < 100000; index++) {
	}
	assert(app_id == snapshot);
	atomic_store(&g_stop_updater, true);
	assert(pthread_join(updater, NULL) == 0);
}

int main(void)
{
	mm_loadable_domain_initialize();
	test_xip_container();
	test_common_identity_quiescence();
	return 0;
}
