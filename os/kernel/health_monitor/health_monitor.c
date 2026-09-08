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
#include "health_monitor_access.h"
#ifdef CONFIG_SYSTEM_REBOOT_REASON
#include <tinyara/reboot_reason.h>
#include <arch/reboot_reason.h>
#endif

#if defined(CONFIG_SCHED_TICKLESS) || defined(CONFIG_SCHED_TICKSUPPRESS) || defined(CONFIG_APP_BINARY_SEPARATION) || defined(CONFIG_SUPPRESS_INTERRUPTS) || defined(CONFIG_SUPPRESS_TIMER_INTS)
#error "Health Monitor requires real periodic ticks without binary teardown"
#endif
#if defined(CONFIG_SMP) && !defined(CONFIG_ARCH_CHIP_AMEBASMART)
#error "Health Monitor SMP requires the coherent amebasmart port"
#endif

/* Every shared word has exactly one logical writer. No shared RMW, atomics,
 * scheduler locks, waiting for readers, or snapshot retry loops. The task
 * scheduler serializes execution of one task across migration and PID reuse.
 * Local IRQ exclusion serializes producers on each CPU, including release.
 */
#ifdef CONFIG_SMP
#define HEALTH_CPUS CONFIG_SMP_NCPUS
#else
#define HEALTH_CPUS 1
#endif
#define HEALTH_QUEUE_SIZE (CONFIG_MAX_TASKS + 1)
#define HEALTH_PUBLISH_GRACE 2
#define HEALTH_TIMEOUT 1
#define HEALTH_EXIT 2
#define HEALTH_PUBLICATION 3
#define HEALTH_EXHAUSTED 4

/* Do not overlay native uint64_t on shared payload: all accesses are words. */
struct health_source_s {
	uint32_t epoch;
	uint32_t seq;
	uint32_t pid;
	uint32_t active;
	uint32_t deadline_low;
	uint32_t deadline_high;
	uint32_t timeout_low;
	uint32_t timeout_high;
};

struct health_snapshot_s {
	uint32_t epoch;
	uint32_t seq;
	pid_t pid;
	bool active;
	uint64_t deadline;
	uint64_t timeout;
};

struct health_channel_s {
	uint32_t head;                 /* Local CPU producer only */
	uint32_t tail;                 /* CPU0 consumer only */
	uint32_t slots[HEALTH_QUEUE_SIZE];
	uint32_t progress_epoch;
	uint32_t progress;             /* Odd during a local API/release */
	uint32_t fault;                /* Published last, sticky per CPU */
	uint32_t fault_pid;
	uint32_t fault_low;
	uint32_t fault_high;
};

struct health_cache_s {
	uint64_t deadline;
	pid_t pid;
	unsigned int position;
	bool active;
	bool uncertain;
	uint32_t uncertain_seq;
	uint32_t uncertain_epoch;
	uint64_t uncertain_since;
};

struct health_node_s {
	uint64_t deadline;             /* Cached lower bound or retry time */
	unsigned int slot;
};

static struct health_source_s g_sources[CONFIG_MAX_TASKS];
static struct health_channel_s g_channels[HEALTH_CPUS];
static uint32_t g_time_seq;
static uint32_t g_time_low;
static uint32_t g_time_high;
static uint32_t g_shutdown;        /* CPU0 publishes; other CPUs read only */

/* The rest is PRIVATE to CPU0. No task-side reads, even on CPU0. */
static struct health_cache_s g_cache[CONFIG_MAX_TASKS];
static struct health_node_s g_heap[CONFIG_MAX_TASKS];
static unsigned int g_count;
static uint64_t g_now;
static unsigned int g_fault;
static pid_t g_fault_pid;
static uint64_t g_fault_deadline;
static uint32_t g_fault_slot;
static unsigned int g_fault_cpu;
static uint32_t g_progress_seen[HEALTH_CPUS];
static uint32_t g_progress_epoch_seen[HEALTH_CPUS];
static uint64_t g_progress_since[HEALTH_CPUS];

static uint64_t health_words(uint32_t low, uint32_t high)
{
	return ((uint64_t)high << 32) | low;
}

static void health_report(struct health_channel_s *channel, unsigned int reason,
						  pid_t pid, uint64_t deadline)
{
	/* All producers on this channel are serialized by LOCAL IRQ masking.
	 * A stalled producer never makes the reader wait for payload completion.
	 */
	if (!health_load(&channel->fault)) {
		health_store(&channel->fault_pid, (uint32_t)pid);
		health_store(&channel->fault_low, (uint32_t)deadline);
		health_store(&channel->fault_high, (uint32_t)(deadline >> 32));
		health_barrier();
		health_store(&channel->fault, reason);
	}
}

static void health_fault(unsigned int reason, pid_t pid, uint64_t deadline)
{
	if (!g_fault) {
		g_fault_pid = pid;
		g_fault_deadline = deadline;
		g_fault_slot = pid > 0 ? PIDHASH(pid) : UINT32_MAX;
		g_fault_cpu = 0;
		g_fault = reason;
		health_barrier();
		health_store(&g_shutdown, 1);
	}
}

static bool health_clock(uint64_t *now)
{
	uint32_t seq = health_load(&g_time_seq);
	uint32_t low;
	uint32_t high;
	if (seq & 1) {
		return false;
	}
	health_barrier();
	low = health_load(&g_time_low);
	high = health_load(&g_time_high);
	health_barrier();
	if (seq != health_load(&g_time_seq)) {
		return false;
	}
	*now = health_words(low, high);
	return true;
}

static bool health_snapshot(unsigned int slot, struct health_snapshot_s *out)
{
	struct health_source_s *source = &g_sources[slot];
	uint32_t epoch = health_load(&source->epoch);
	uint32_t seq;
	uint32_t after;
	uint32_t low;
	uint32_t high;
	/* Epoch brackets the low sequence, including its wrap. Neither is a
	 * shared 64-bit access. Exhaustion is after 2^63 publications, not weeks
	 * of high-rate kicks. No snapshot waits for a writer or retries.
	 */
	health_barrier();
	seq = health_load(&source->seq);
	if (seq & 1) {
		return false;
	}
	health_barrier();
	out->epoch = epoch;
	out->seq = seq;
	out->pid = (pid_t)health_load(&source->pid);
	out->active = health_load(&source->active) != 0;
	low = health_load(&source->deadline_low);
	high = health_load(&source->deadline_high);
	out->deadline = health_words(low, high);
	low = health_load(&source->timeout_low);
	high = health_load(&source->timeout_high);
	out->timeout = health_words(low, high);
	health_barrier();
	after = health_load(&source->seq);
	health_barrier();
	return seq == after && epoch == health_load(&source->epoch);
}

static void health_publish(unsigned int slot, const struct health_snapshot_s *value, bool checkpoint_only)
{
	struct health_source_s *source = &g_sources[slot];
	health_store(&source->seq, value->seq + 1);
	health_barrier();
	if (value->seq == UINT32_MAX - 1) {
		health_store(&source->epoch, value->epoch + 1);
	}
	if (!checkpoint_only) {
		health_store(&source->pid, (uint32_t)value->pid);
		health_store(&source->active, value->active);
		health_store(&source->timeout_low, (uint32_t)value->timeout);
		health_store(&source->timeout_high, (uint32_t)(value->timeout >> 32));
	}
	health_store(&source->deadline_low, (uint32_t)value->deadline);
	health_store(&source->deadline_high, (uint32_t)(value->deadline >> 32));
	health_barrier();
	health_store(&source->seq, value->seq + 2);
}

static unsigned int health_next(unsigned int index)
{
	return index + 1 == HEALTH_QUEUE_SIZE ? 0 : index + 1;
}

/* Caller already masked local IRQs, so no producer can overlap this call. */
static int health_begin(struct health_channel_s *channel, pid_t pid, uint32_t *progress)
{
	if (health_load(&g_shutdown) || health_load(&channel->fault)) {
		return -ESHUTDOWN;
	}
	*progress = health_load(&channel->progress);
	if ((*progress == UINT32_MAX - 1 && health_load(&channel->progress_epoch) == UINT32_MAX) || (*progress & 1)) {
		health_report(channel, HEALTH_EXHAUSTED, pid, 0);
		return -EOVERFLOW;
	}
	health_store(&channel->progress, *progress + 1);
	health_barrier();
	return 0;
}

static void health_end(struct health_channel_s *channel, uint32_t progress)
{
	if (progress == UINT32_MAX - 1) {
		health_store(&channel->progress_epoch, health_load(&channel->progress_epoch) + 1);
	}
	health_barrier();
	health_store(&channel->progress, progress + 2);
}

/* op: start=0, kick=1, stop=2. API writes no CPU0-owned data. */
static int health_update(unsigned int op, uint32_t timeout_ms)
{
	irqstate_t flags;
	struct health_channel_s *channel;
	struct health_snapshot_s value;
	uint64_t now;
	uint32_t progress;
	unsigned int head = 0;
	unsigned int next = 0;
	unsigned int slot;
	pid_t pid;
	int ret;

	if (up_interrupt_context()) {
		return -EPERM;
	}
	flags = irqsave();
	pid = sched_self()->pid;
	slot = PIDHASH(pid);
	channel = &g_channels[up_cpu_index()];
	ret = health_begin(channel, pid, &progress);
	if (ret) {
		goto out;
	}
	if (pid <= 0 || (op == 0 && !timeout_ms)) {
		ret = -EINVAL;
		goto done;
	}
	if (!health_clock(&now)) {
		ret = -EAGAIN;
		goto done;
	}
	/* This slot has only one task writer at a time; a failed self snapshot
	 * indicates an interrupted/abandoned publication, not normal contention.
	 */
	if (!health_snapshot(slot, &value)) {
		health_report(channel, HEALTH_PUBLICATION, pid, 0);
		ret = -EIO;
		goto done;
	}
	if (value.seq == UINT32_MAX - 1 && value.epoch == UINT32_MAX) {
		health_report(channel, HEALTH_EXHAUSTED, pid, value.deadline);
		ret = -EOVERFLOW;
		goto done;
	}
	if (op == 0) {
		if (value.active) {
			ret = -EALREADY;
			goto done;
		}
		value.timeout = ((uint64_t)timeout_ms * 1000 + USEC_PER_TICK - 1) / USEC_PER_TICK;
	} else {
		if (!value.active || value.pid != pid) {
			ret = -ENOENT;
			goto done;
		}
		if (now >= value.deadline) {
			health_report(channel, HEALTH_TIMEOUT, pid, value.deadline);
			ret = -ETIMEDOUT;
			goto done;
		}
	}
	/* Queue capacity is reserved BEFORE changing the source. A failed stop
	 * remains armed; a failed start creates no contract. No spin on full.
	 */
	if (op != 1) {
		head = health_load(&channel->head);
		next = health_next(head);
		if (next == health_load(&channel->tail)) {
			ret = -ENOSPC;
			goto done;
		}
		health_barrier();
	}
	if (op != 2) {
		if (UINT64_MAX - now < value.timeout) {
			health_report(channel, HEALTH_EXHAUSTED, pid, 0);
			ret = -EOVERFLOW;
			goto done;
		}
		value.deadline = now + value.timeout;
	}
	value.pid = pid;
	value.active = op != 2;
	health_publish(slot, &value, op == 1);
	if (op != 1) {
		/* Notifications mean 'reread slot', not 'replay start/stop'. Thus
		 * migration, restart and PID reuse cannot reorder old commands onto
		 * a newer registration. Successful stop has checked its deadline.
		 */
		health_store(&channel->slots[head], slot);
		health_barrier();
		health_store(&channel->head, next);
	}
	ret = 0;
done:
	health_end(channel, progress);
out:
	irqrestore(flags);
	return ret;
}

int health_monitor_start(uint32_t timeout_ms)
{
	return health_update(0, timeout_ms);
}

int health_monitor_kick(void)
{
	return health_update(1, 0);
}

int health_monitor_stop(void)
{
	return health_update(2, 0);
}

void health_monitor_release(pid_t pid)
{
	irqstate_t flags = irqsave();
	struct health_channel_s *channel = &g_channels[up_cpu_index()];
	struct health_snapshot_s value;
	uint32_t progress;
	/* Scheduler has stopped the target, and has not made its PID reusable.
	 * Never become a second writer to the task's source publication area.
	 */
	if (!health_begin(channel, pid, &progress)) {
		if (!health_snapshot(PIDHASH(pid), &value)) {
			health_report(channel, HEALTH_PUBLICATION, pid, 0);
		} else if (value.active && value.pid == pid) {
			health_report(channel, HEALTH_EXIT, pid, value.deadline);
		}
		health_end(channel, progress);
	}
	irqrestore(flags);
}

static void health_put(unsigned int pos, struct health_node_s node)
{
	g_heap[pos] = node;
	g_cache[node.slot].position = pos;
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

static void health_reorder(unsigned int pos, struct health_node_s node)
{
	if (pos && node.deadline < g_heap[(pos - 1) / 2].deadline) {
		health_up(pos, node);
	} else {
		health_down(pos, node);
	}
}

static void health_apply(unsigned int slot, const struct health_snapshot_s *value)
{
	struct health_cache_s *cache = &g_cache[slot];
	struct health_node_s node;
	unsigned int pos = cache->position;
	cache->uncertain = false;
	if (!value->active) {
		if (cache->active) {
			cache->active = false;
			node = g_heap[--g_count];
			if (pos < g_count) {
				health_reorder(pos, node);
			}
		}
		return;
	}
	cache->pid = value->pid;
	cache->deadline = value->deadline;
	node.slot = slot;
	node.deadline = value->deadline;
	if (!cache->active) {
		cache->active = true;
		health_up(g_count++, node);
	} else {
		health_reorder(pos, node);
	}
}

/* Turn an unreadable membership hint into a CPU0 heap retry. Do not leave
 * it at the queue head: a busy writer must not hide another task's start.
 */
static void health_pending(unsigned int slot)
{
	struct health_cache_s *cache = &g_cache[slot];
	struct health_node_s node;
	if (!cache->active) {
		cache->active = true;
		cache->pid = -1;
		cache->deadline = 0;
		node.slot = slot;
		node.deadline = g_now;
		health_up(g_count++, node);
	} else if (g_heap[cache->position].deadline > g_now) {
		node.slot = slot;
		node.deadline = g_now;
		health_up(cache->position, node);
	}
}

static void health_channels(void)
{
	unsigned int cpu;
	for (cpu = 0; cpu < HEALTH_CPUS && !g_fault; cpu++) {
		struct health_channel_s *channel = &g_channels[cpu];
		uint32_t fault = health_load(&channel->fault);
		uint32_t progress_epoch = health_load(&channel->progress_epoch);
		uint32_t progress;
		bool progress_valid;
		unsigned int head;
		unsigned int tail;
		if (fault) {
			health_barrier();
			health_fault(fault, (pid_t)health_load(&channel->fault_pid),
				health_words(health_load(&channel->fault_low), health_load(&channel->fault_high)));
			g_fault_cpu = cpu;
			break;
		}
		health_barrier();
		progress = health_load(&channel->progress);
		health_barrier();
		progress_valid = progress_epoch == health_load(&channel->progress_epoch);
		if (!progress_valid || !(progress & 1) || progress != g_progress_seen[cpu] || progress_epoch != g_progress_epoch_seen[cpu]) {
			g_progress_seen[cpu] = progress;
			g_progress_epoch_seen[cpu] = progress_epoch;
			g_progress_since[cpu] = g_now;
		} else if (g_now - g_progress_since[cpu] >= HEALTH_PUBLISH_GRACE) {
			health_fault(HEALTH_PUBLICATION, -1, 0);
			g_fault_cpu = cpu;
			break;
		}
		/* Snapshot head ONCE: producer churn cannot extend this tick's loop.
		 * The producer cannot overwrite any entry until we publish tail.
		 */
		head = health_load(&channel->head);
		tail = health_load(&channel->tail);
		health_barrier();
		while (tail != head) {
			unsigned int slot = health_load(&channel->slots[tail]);
			struct health_snapshot_s value;
			if (!health_snapshot(slot, &value)) {
				health_pending(slot);
			} else {
				health_apply(slot, &value);
			}
			tail = health_next(tail);
			health_barrier();
			health_store(&channel->tail, tail);
		}
	}
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
	health_text(" slot=");
	health_hex(g_fault_slot);
	health_text(" report_cpu=");
	health_hex(g_fault_cpu);
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
		struct health_cache_s *source = &g_cache[g_heap[i].slot];
		health_text("HM registered pid=");
		health_hex(source->pid);
		health_text(" slot=");
		health_hex(g_heap[i].slot);
		health_text(" deadline=");
		health_hex(source->deadline);
		health_text("\n");
	}
	/* A fault mailbox can win before membership hints have been consumed.
	 * Inspect sources too, so unindexed registrations survive diagnostics.
	 * No retry on a torn publication; label it and continue to other slots.
	 */
	for (i = 0; i < CONFIG_MAX_TASKS; i++) {
		struct health_snapshot_s value;
		if (!health_snapshot(i, &value)) {
			health_text("HM source unreadable slot=");
			health_hex(i);
			health_text("\n");
		} else if (value.active) {
			health_text("HM source pid=");
			health_hex(value.pid);
			health_text(" slot=");
			health_hex(i);
			health_text(" deadline=");
			health_hex(value.deadline);
			health_text(" timeout=");
			health_hex(value.timeout);
			health_text("\n");
		}
	}
	for (i = 0; i < HEALTH_CPUS; i++) {
		struct health_channel_s *channel = &g_channels[i];
		health_text("HM channel raw cpu=");
		health_hex(i);
		health_text(" head=");
		health_hex(health_load(&channel->head));
		health_text(" tail=");
		health_hex(health_load(&channel->tail));
		health_text(" progress_epoch=");
		health_hex(health_load(&channel->progress_epoch));
		health_text(" progress=");
		health_hex(health_load(&channel->progress));
		health_text(" fault=");
		health_hex(health_load(&channel->fault));
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
	uint32_t seq;
	if (up_cpu_index() != 0) {
		return;
	}
	/* CPU0 alone owns time, heap and the first confirmed fault. */
	seq = health_load(&g_time_seq);
	if (g_now == UINT64_MAX || ((uint32_t)(g_now + 1) == 0 && seq >= UINT32_MAX - 1)) {
		health_fault(HEALTH_EXHAUSTED, -1, 0);
	} else {
		g_now++;
		/* A single word update needs no sequence change. Protect only the
		 * high-word carry, avoiding sequence exhaustion after 2^31 ticks.
		 */
		if ((uint32_t)g_now == 0) {
			health_store(&g_time_seq, seq + 1);
			health_barrier();
			health_store(&g_time_low, 0);
			health_store(&g_time_high, (uint32_t)(g_now >> 32));
			health_barrier();
			health_store(&g_time_seq, seq + 2);
		} else {
			health_store(&g_time_low, (uint32_t)g_now);
		}
	}
	health_channels();
	while (!g_fault && g_count && g_heap[0].deadline <= g_now) {
		struct health_node_s root = g_heap[0];
		struct health_cache_s *cache = &g_cache[root.slot];
		struct health_snapshot_s value;
		if (!health_snapshot(root.slot, &value)) {
			uint32_t epoch = health_load(&g_sources[root.slot].epoch);
			uint32_t version;
			bool valid;
			health_barrier();
			version = health_load(&g_sources[root.slot].seq);
			health_barrier();
			valid = epoch == health_load(&g_sources[root.slot].epoch);
			if (!valid || !cache->uncertain || cache->uncertain_seq != version || cache->uncertain_epoch != epoch) {
				cache->uncertain = true;
				cache->uncertain_seq = version;
				cache->uncertain_epoch = epoch;
				cache->uncertain_since = g_now;
			} else if (g_now - cache->uncertain_since >= HEALTH_PUBLISH_GRACE) {
				health_fault(HEALTH_PUBLICATION, cache->pid, cache->deadline);
				g_fault_slot = root.slot;
				break;
			}
			/* A changing version proves the task writer completed publication
			 * (kick/stop checks lateness before writing). Do not misclassify
			 * healthy churn as a stuck writer. An unchanged unreadable version
			 * has a finite grace. Every other expired root is checked now.
			 */
			if (g_now == UINT64_MAX) {
				health_fault(HEALTH_EXHAUSTED, cache->pid, cache->deadline);
				break;
			}
			root.deadline = g_now + 1;
			health_down(0, root);
		} else if (value.active && value.deadline <= g_now) {
			/* This accepted snapshot is the timeout decision point. A later
			 * concurrent publication does not retract an observed failure.
			 */
			health_fault(HEALTH_TIMEOUT, value.pid, value.deadline);
		} else {
			health_apply(root.slot, &value);
		}
	}
	if (g_fault) {
		health_fatal();
	}
}
