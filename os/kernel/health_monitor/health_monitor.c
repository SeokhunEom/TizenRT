/****************************************************************************
 *
 * Copyright 2026 Samsung Electronics All Rights Reserved.
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
#include <tinyara/config.h>
#include <tinyara/arch.h>
#include <tinyara/irq.h>
#include <tinyara/clock.h>
#include <tinyara/sched.h>
#include <tinyara/health_monitor.h>
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include "health_monitor_internal.h"
#ifdef CONFIG_SYSTEM_REBOOT_REASON
#include <tinyara/reboot_reason.h>
#include <arch/reboot_reason.h>
#endif

/* Local IRQ exclusion is sufficient ONLY on a uniprocessor. Never replace it
 * with a scheduler-global lock: callers may already own locks we diagnose.
 */
#if defined(CONFIG_SMP) || defined(CONFIG_SCHED_TICKLESS) || defined(CONFIG_SCHED_TICKSUPPRESS) || defined(CONFIG_APP_BINARY_SEPARATION)
#error "Health Monitor core requires UP periodic ticks without binary teardown"
#endif

struct health_source_s {
	uint64_t deadline;
	uint64_t timeout;
	pid_t pid;
	unsigned int position;
	bool active;
};

struct health_node_s {
	uint64_t deadline;             /* Lower bound, stale after kick */
	unsigned int slot;
};

static struct health_source_s g_sources[CONFIG_MAX_TASKS];
static struct health_node_s g_heap[CONFIG_MAX_TASKS];
static unsigned int g_count;
static uint64_t g_now;             /* Awake ticks; never sleep compensation */
static unsigned int g_fault;       /* First fault wins: 1 timeout, 2 exit */
static pid_t g_fault_pid;
static uint64_t g_fault_deadline;

static void health_fault(unsigned int reason, struct health_source_s *source)
{
	if (!g_fault) {
		g_fault_pid = source->pid;
		g_fault_deadline = source->deadline;
		g_fault = reason;
	}
}

static void health_put(unsigned int pos, struct health_node_s node)
{
	g_heap[pos] = node;
	g_sources[node.slot].position = pos;
}

static void health_up(unsigned int pos, struct health_node_s node)
{
	while (pos > 0) {
		unsigned int parent = (pos - 1) / 2;
		if (g_heap[parent].deadline <= node.deadline) {
			break;
		}
		health_put(pos, g_heap[parent]);
		pos = parent;
	}
	health_put(pos, node);
}

static void health_down(unsigned int pos, struct health_node_s node)
{
	while (pos < g_count / 2) {
		unsigned int child = pos * 2 + 1;
		if (child + 1 < g_count && g_heap[child + 1].deadline < g_heap[child].deadline) {
			child++;
		}
		if (node.deadline <= g_heap[child].deadline) {
			break;
		}
		health_put(pos, g_heap[child]);
		pos = child;
	}
	health_put(pos, node);
}

/* Nonzero errno values are returned negated, without libc errno access. */
int health_monitor_start(uint32_t timeout_ms)
{
	irqstate_t flags;
	struct health_source_s *source;
	struct health_node_s node;
	pid_t pid;
	int ret = 0;

	if (up_interrupt_context()) {
		return -EPERM;
	}
	flags = irqsave();
	pid = sched_self()->pid;
	source = &g_sources[PIDHASH(pid)];
	if (g_fault) {
		ret = -ESHUTDOWN;
	} else if (!timeout_ms || pid <= 0) {
		ret = -EINVAL;
	} else if (source->active) {
		ret = -EALREADY;
	} else {
		source->timeout = ((uint64_t)timeout_ms * 1000 + USEC_PER_TICK - 1) / USEC_PER_TICK;
		source->deadline = g_now + source->timeout;
		source->pid = pid;
		source->active = true;
		node.slot = PIDHASH(pid);
		node.deadline = source->deadline;
		health_up(g_count++, node);
	}
	irqrestore(flags);
	return ret;
}

static int health_update(bool stop)
{
	irqstate_t flags;
	struct health_source_s *source;
	pid_t pid;
	int ret = 0;

	if (up_interrupt_context()) {
		return -EPERM;
	}
	flags = irqsave();
	pid = sched_self()->pid;
	source = &g_sources[PIDHASH(pid)];
	if (g_fault) {
		ret = -ESHUTDOWN;
	} else if (!source->active || source->pid != pid) {
		ret = -ENOENT;
	} else if (g_now >= source->deadline) {
		/* A late checkpoint/stop cannot erase an already broken contract. */
		health_fault(1, source);
		ret = -ETIMEDOUT;
	} else if (stop) {
		unsigned int pos = source->position;
		struct health_node_s last = g_heap[--g_count];
		source->active = false;
		if (pos < g_count) {
			if (pos && last.deadline < g_heap[(pos - 1) / 2].deadline) {
				health_up(pos, last);
			} else {
				health_down(pos, last);
			}
		}
	} else {
		source->deadline = g_now + source->timeout;
	}
	irqrestore(flags);
	return ret;
}

int health_monitor_stop(void)
{
	return health_update(true);
}

int health_monitor_kick(void)
{
	return health_update(false);
}

/* Called before releasing PID/TCB; save only scalar evidence, never a TCB
 * pointer. Fatal diagnostics run from the next tick, outside teardown locks.
 */
void health_monitor_release(pid_t pid)
{
	irqstate_t flags = irqsave();
	struct health_source_s *source = &g_sources[PIDHASH(pid)];
	if (source->active && source->pid == pid) {
		health_fault(2, source);
	}
	irqrestore(flags);
}

/* Raw bounded header precedes printf, scheduler traversal and panic. up_putc
 * is the architecture polling console interface; no stdio/log-buffer access.
 */
static void health_text(const char *s)
{
	while (*s) {
		up_putc(*s++);
	}
}

static void health_hex(uint64_t value)
{
	int shift;
	for (shift = 60; shift >= 0; shift -= 4) {
		up_putc("0123456789abcdef"[(value >> shift) & 15]);
	}
}

static void health_dump_task(struct tcb_s *tcb, void *arg)
{
	(void)arg;
	health_text("HM task pid=");
	health_hex(tcb->pid);
	health_text(" state=");
	health_hex(tcb->task_state);
	health_text(" priority=");
	health_hex(tcb->sched_priority);
	health_text(" schedlock=");
	health_hex(tcb->lockcount);
	health_text(" waitsem=");
	health_hex((uintptr_t)tcb->waitsem);
	health_text(" stack=");
	health_hex((uintptr_t)tcb->stack_alloc_ptr);
	health_text("\n");
}

static void health_fatal(void)
{
	unsigned int i;
	health_text("HM fault=");
	health_hex(g_fault);
	health_text(" pid=");
	health_hex(g_fault_pid);
	health_text(" now=");
	health_hex(g_now);
	health_text(" deadline=");
	health_hex(g_fault_deadline);
	health_text("\n");
#ifdef CONFIG_SYSTEM_REBOOT_REASON
	/* Reuse the existing reason until board-specific Health Failure reasons
	 * are provisioned. The raw header distinguishes timeout from exit.
	 */
	up_reboot_reason_write(REBOOT_SYSTEM_WATCHDOG);
#endif
	/* Full walks are intentionally reserved for the fatal phase. */
	for (i = 0; i < g_count; i++) {
		struct health_source_s *source = &g_sources[g_heap[i].slot];
		health_text("HM registered pid=");
		health_hex(source->pid);
		health_text(" deadline=");
		health_hex(source->deadline);
		health_text("\n");
	}
	sched_foreach(health_dump_task, NULL);
	PANIC();
	for (;;) {
		/* Even a board panic implementation that returns must never resume. */
	}
}

void health_monitor_tick(void)
{
	/* Called first in the periodic tick ISR, before legacy watchdog feeding.
	 * Tasks cannot publish while this UP interrupt executes. Each stale root
	 * is repaired at most once per tick, bounding work by N * log2(N).
	 */
	g_now++;
	while (!g_fault && g_count && g_heap[0].deadline <= g_now) {
		struct health_node_s root = g_heap[0];
		struct health_source_s *source = &g_sources[root.slot];
		if (source->deadline <= g_now) {
			health_fault(1, source);
			break;
		}
		root.deadline = source->deadline;
		health_down(0, root);
	}
	if (g_fault) {
		health_fatal();
	}
}
