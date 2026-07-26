#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>

#include <tinyara/arch.h>
#include <tinyara/binfmt/binfmt.h>
#include <tinyara/kmalloc.h>
#include <tinyara/mm/mm.h>
#include <tinyara/sched.h>

static struct task_tcb_s *g_launched;
static struct tcb_s g_parent;
static const char *g_heap_owner;
static int g_activated;

void *kmm_zalloc(size_t size)
{
	void *memory = calloc(1, size);
	assert(memory != NULL);
	return memory;
}

void kmm_free(void *memory)
{
	free(memory);
}

void kumm_free(void *memory)
{
	free(memory);
}

int get_errno(void)
{
	return 5;
}

int mm_initialize(struct mm_heap_s *heap, void *heap_start, size_t heap_size)
{
	assert(heap != NULL);
	assert(heap_start != NULL);
	assert(heap_size >= 4096u);
	return OK;
}

void mm_add_app_heap_list(struct mm_heap_s *heap, char *app_name)
{
	assert(heap != NULL);
	g_heap_owner = app_name;
}

void mm_remove_app_heap_list(struct mm_heap_s *heap)
{
	(void)heap;
}

struct tcb_s *sched_self(void)
{
	return &g_parent;
}

int up_create_stack(struct tcb_s *tcb, size_t stack_size, int task_type)
{
	(void)task_type;
	tcb->stack_alloc_ptr = malloc(stack_size);
	return tcb->stack_alloc_ptr == NULL ? -1 : OK;
}

int task_init(struct tcb_s *tcb, const char *name, int priority, void *stack,
		size_t stack_size, int (*entry)(int, char **), char *const *argv)
{
	size_t index;

	(void)priority;
	(void)stack;
	(void)stack_size;
	(void)entry;
	(void)argv;
	tcb->pid = 37;
	for (index = 0; index < CONFIG_TASK_NAME_SIZE && name[index] != '\0';
			index++) {
		tcb->name[index] = name[index];
	}
	tcb->name[index] = '\0';
	g_launched = (struct task_tcb_s *)tcb;
	return OK;
}

int task_activate(struct tcb_s *tcb)
{
	assert(tcb == &g_launched->cmn);
	g_activated = 1;
	return OK;
}

int sched_releasetcb(struct tcb_s *tcb, uint8_t task_type)
{
	(void)tcb;
	(void)task_type;
	return OK;
}

#ifndef TASK_EXEC_SOURCE
#define TASK_EXEC_SOURCE "../../../binfmt/binfmt_execmodule.c"
#endif
#include TASK_EXEC_SOURCE

#include <string.h>

static int app_main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	return 0;
}

int main(void)
{
	const uintptr_t base_address = 0x10000000u;
	const size_t region_size = 65536u;
	void *region;
	struct binary_s binary = { 0 };
	int pid;

	region = mmap((void *)base_address, region_size, PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANON | MAP_FIXED, -1, 0);
	assert(region == (void *)base_address);

	binary.filename = "/tmp/app1";
	binary.entrypt = app_main;
	binary.stacksize = 2048u;
	binary.priority = 100u;
	binary.binary_idx = 1u;
	binary.bin_ver = 260718u;
	binary.bin_name = "app1";
	binary.sections[BIN_HEAP] = (uint32_t)base_address;
	binary.sizes[BIN_HEAP] = 4096u;
	binary.sections[BIN_DATA] = (uint32_t)(base_address + 8192u);
	binary.sections[BIN_TEXT] = (uint32_t)(base_address + 12288u);

	pid = exec_module(&binary);
	if (pid <= 0 || g_launched == NULL || g_launched->cmn.app_id != 1u ||
			strcmp(g_launched->cmn.name, "app1") != 0 ||
			g_heap_owner == NULL || strcmp(g_heap_owner, "app1") != 0 ||
			g_activated != 1) {
		fprintf(stderr,
				"OWNER_NAME_ASSERT pid=%d app_id=%lu name=%s owner=%s activated=%d\n",
				pid,
				g_launched == NULL ? 0ul : (unsigned long)g_launched->cmn.app_id,
				g_launched == NULL ? "<none>" : g_launched->cmn.name,
				g_heap_owner == NULL ? "<none>" : g_heap_owner, g_activated);
		return 3;
	}

	printf("NO_BM_EXEC pid=%d app_id=%lu name=%s owner=%s activated=%d\n",
			pid, (unsigned long)g_launched->cmn.app_id, g_launched->cmn.name,
			g_heap_owner, g_activated);
	free(g_launched->cmn.stack_alloc_ptr);
	free(g_launched);
	assert(munmap(region, region_size) == 0);
	return 0;
}
