/****************************************************************************
 *
 * Copyright 2019 Samsung Electronics All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
 * either express or implied. See the License for the specific
 * language governing permissions and limitations under the License.
 *
 ****************************************************************************/
/****************************************************************************
 * os/binfmt/binfmt_exit.c
 *
 *   Copyright (C) 2013, 2016-2018 Gregory Nutt. All rights reserved.
 *   Author: Gregory Nutt <gnutt@nuttx.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name NuttX nor the names of its contributors may be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <tinyara/config.h>

#include <sys/types.h>
#include <assert.h>
#include <debug.h>
#include <queue.h>
#include <tinyara/sched.h>
#include <tinyara/wqueue.h>
#ifdef CONFIG_BINARY_MANAGER
#include "binary_manager/binary_manager_internal.h"
#endif
#ifdef CONFIG_SAVE_BIN_SECTION_ADDR
#include "libelf/libelf.h"
#endif

#include <tinyara/mm/mm.h>
#include <tinyara/kmalloc.h>
#include <tinyara/binfmt/binfmt.h>

#include "binfmt.h"

#if defined(CONFIG_BINFMT_LOADABLE) && defined(CONFIG_BINFMT_CONSTRUCTORS)
#error "Deferred app unload requires an app-context destructor provider before BINFMT_CONSTRUCTORS"
#endif

#ifdef CONFIG_BINFMT_LOADABLE
extern volatile sq_queue_t g_delayed_kufree;

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: binfmt_exit
 *
 * Description:
 *   This function may be called when a tasked loaded into RAM exits.
 *   This function will unload the module when the task exits and reclaim
 *   all resources used by the module.
 *
 * Input Parameters:
 *   bin - This structure must have been allocated with kmm_malloc() and must
 *         persist until the task unloads
 *
 * Returned Value:
 *   This is a internal function so it follows the convention that
 *   0 (OK) is returned on success and a negated errno is returned on
 *   failure.
 *
 ****************************************************************************/

/* The queue owns each binary until its final group member no longer exists.
 * It is also safe when group_leave runs from task_exit's invalid-current-TCB
 * transition: enqueueing does not take an allocator or cancellation lock.
 */
static struct binary_s *g_exiting_bins;

void binfmt_exit_deferred(FAR struct binary_s *bin, pid_t pid)
{
	irqstate_t flags = enter_critical_section();
	struct binary_s *queued;
	for (queued = g_exiting_bins; queued; queued = queued->exit_next) {
		if (queued == bin) {
			leave_critical_section(flags);
			return;
		}
	}
	bin->exit_pid = sched_gettcb(pid) ? pid : -1;
	bin->exit_next = g_exiting_bins;
	g_exiting_bins = bin;
#ifdef CONFIG_SCHED_WORKQUEUE
	work_signal(LPWORK);
#endif
	leave_critical_section(flags);
}

/* Called with interrupts disabled before this PID can be assigned again. */
void binfmt_exit_releasepid(pid_t pid)
{
	struct binary_s *bin;
	for (bin = g_exiting_bins; bin; bin = bin->exit_next) {
		if (bin->exit_pid == pid) { bin->exit_pid = -1; }
	}
#ifdef CONFIG_SCHED_WORKQUEUE
	/* The last reference may belong to a different task group than the
	 * group that enqueued the binary. Wake after every PID release. */
	if (g_exiting_bins) { work_signal(LPWORK); }
#endif
}

struct binfmt_live_s {
	struct binary_s *bin;
	bool live;
};

static void binfmt_check_live(FAR struct tcb_s *tcb, FAR void *arg)
{
	struct binfmt_live_s *check = arg;
	if ((uintptr_t)tcb->uheap == (uintptr_t)check->bin->uheap) {
		check->live = true;
	}
#ifdef CONFIG_BINARY_MANAGER
	if (check->bin->binary_idx > 0 && tcb->app_id == check->bin->binary_idx) {
		check->live = true;
	}
#endif
}

static bool binfmt_still_live(struct binary_s *bin)
{
	struct binfmt_live_s check = { bin, false };
	if (bin->exit_pid >= 0) { return true; }
#ifdef CONFIG_BINARY_MANAGER
	if (BIN_RTLIST(bin->binary_idx) || BIN_NRTLIST(bin->binary_idx)) {
		return true;
	}
#endif
	/* Binary-list unlink happens in exithook, before the CPU stops using
	 * that task's stack. Include unlinked/exiting TCBs and separate task
	 * groups that inherited the same app heap until their PID is released. */
	sched_foreach(binfmt_check_live, &check);
	return check.live;
}

void binfmt_exit_cleanup(void)
{
	struct binary_s *bin;
	struct binary_s **link;
	irqstate_t flags;

	for (;;) {
		flags = enter_critical_section();
		link = &g_exiting_bins;
		while (*link && binfmt_still_live(*link)) {
			link = &(*link)->exit_next;
		}
		bin = *link;
		if (bin) {
			*link = bin->exit_next;
			bin->exit_next = NULL;
		}
		leave_critical_section(flags);
		if (!bin) { return; }
		int ret = binfmt_exit(bin);
		if (ret < 0) {
			/* Keep the image unavailable for reload if its unload hook failed. */
			berr("ERROR: deferred module unload failed: %d\n", ret);
		}
	}
}

/* Called by unload_module after module callbacks but before section RAM is
 * released. This ordering also covers exec_module error cleanup.
 */
void binfmt_release_appheap(FAR struct binary_s *bin)
{
	irqstate_t flags;
	sq_entry_t *address;
	sq_entry_t *next;
	uintptr_t start;
	uintptr_t end;

#ifdef CONFIG_SUPPORT_COMMON_BINARY
	if (!bin->islibrary && g_lib_binp) {
		uint32_t *table = (uint32_t *)(g_lib_binp->sections[BIN_DATA] + 8);
		if (table[bin->binary_idx] == bin->sections[BIN_HEAP]) {
			table[bin->binary_idx] = 0;
		}
	}
#endif
	if (!bin->uheap) { return; }
	start = (uintptr_t)bin->uheap;
	end = start + sizeof(struct mm_heap_s) + bin->sizes[BIN_HEAP];

	/* Save next before removing an entry. Cover the full descriptor + arena
	 * with an exclusive end, and serialize the entire walk with producers.
	 */
	flags = enter_critical_section();
	address = sq_peek(&g_delayed_kufree);
	while (address) {
		next = sq_next(address);
		if (start <= (uintptr_t)address && (uintptr_t)address < end) {
			sq_rem(address, (sq_queue_t *)&g_delayed_kufree);
		}
		address = next;
	}
	leave_critical_section(flags);
	mm_disable_app_heap_list(bin->uheap);
	mm_uninitialize(bin->uheap);
#ifdef CONFIG_OPTIMIZE_APP_RELOAD_TIME
	if (!bin->reload)
#endif
	{
		mm_remove_app_heap_list(bin->uheap);
	}
}

int binfmt_exit(FAR struct binary_s *bin)
{
	int ret;
#ifdef CONFIG_BINARY_MANAGER
	int binary_idx = bin->binary_idx;
#endif
	DEBUGASSERT(bin != NULL);

	/* unload_module owns section RAM and destroys the heap before returning
	 * that RAM. Freeing ramstart again here would be a double free for ELF;
	 * XIP RAM is a fixed linker region and must not be passed to kmm_free.
	 */
	ret = unload_module(bin);
	if (ret < 0) { return ret; }
	elf_delete_bin_section_addr(bin->binary_idx);
#ifdef CONFIG_OPTIMIZE_APP_RELOAD_TIME
	if (!bin->reload)
#endif
	{
#ifdef CONFIG_BINARY_MANAGER
		BIN_LOADINFO(binary_idx) = NULL;
#endif
		kmm_free(bin);
	}
#ifdef CONFIG_BINARY_MANAGER
	/* Publish completion only after the heap, registry and image are gone. */
	binary_manager_clear_bindata(binary_idx);
#endif
	return OK;
}

#endif							/* CONFIG_BINFMT_LOADABLE */
