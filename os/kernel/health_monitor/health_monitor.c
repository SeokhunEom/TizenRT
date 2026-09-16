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
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 ****************************************************************************/

#include <tinyara/config.h>

#include <errno.h>
#include <tinyara/spinlock.h>

#include "sched/sched.h"
#include "health_monitor/health_monitor.h"

/* There are two views of the registration state:
 *
 * 1. The heap, its count and each TCB's state are protected by the health
 *    monitor lock. The root contains the earliest reserved check_at, which
 *    can be earlier than the latest deadline after KICK.
 * 2. sequence/next_count/next_tick form a separate, pointer-free copy of
 *    the root information. A future tick/PM caller can read this hint
 *    without taking the lock or touching a possibly exiting TCB.
 *
 * Only the hint words need __atomic accesses: their reader does not take
 * the writer's lock. Making each word atomic prevents data races on that
 * word, but does not make the whole three-word snapshot atomic. The
 * sequence protocol below detects inconsistent snapshots.
 *
 * Require native lock-free 32-bit atomics at compile time. Do not silently
 * accept an implementation that uses a library lock for these accesses.
 */

_Static_assert(__atomic_always_lock_free(sizeof(uint32_t), 0),
			   "Health monitor requires lock-free 32-bit atomics");

static struct health_monitor_entry_s g_health_heap[HEALTH_MONITOR_HEAP_CAPACITY];
static unsigned int g_health_count;
#ifdef CONFIG_SMP
static spinlock_t g_health_lock = SP_UNLOCKED;
#endif
static uint32_t g_health_sequence;
static uint32_t g_health_next_count;
static uint32_t g_health_next_tick;

/* Mask local IRQs first so an interrupt cannot reenter while this CPU owns
 * the lock. On SMP, the private spinlock also excludes other CPUs; on UP,
 * local IRQ masking alone is sufficient.
 *
 * This does not sleep, but the SMP spinlock can wait for another owner.
 * It is for the thread/lifecycle paths, not the future tick ISR path,
 * which must use a non-waiting trylock when it needs the actual heap.
 * Use non-instrumented operations so note callbacks cannot acquire another
 * lock here. The allowed order is scheduler lock -> health monitor lock;
 * code holding this lock must not acquire the scheduler lock in reverse.
 */

static irqstate_t health_monitor_lock(void)
{
	irqstate_t flags = irqsave();

#ifdef CONFIG_SMP
	spin_lock_wo_note(&g_health_lock);
#endif
	return flags;
}

/* Leave the matching critical section. Release the SMP lock before
 * restoring IRQs, otherwise an interrupt could run while we still own it.
 * Restore the saved state instead of unconditionally enabling IRQs: the
 * caller may already have entered with IRQs masked.
 */

static void health_monitor_unlock(irqstate_t flags)
{
#ifdef CONFIG_SMP
	spin_unlock_wo_note(&g_health_lock);
#endif
	irqrestore(flags);
}

/* Publish the current heap root as a lockless scheduling hint, in O(1).
 * Call only while holding the registry lock, after the heap is consistent.
 * START and unregister publish; KICK does not, because it leaves check_at
 * unchanged. This function neither checks timeouts nor updates deadlines.
 *
 * sequence is a version, not a task count or tick:
 *   even: the published count/tick pair is complete;
 *   odd:  a writer may be changing that pair.
 * For example, an update advances sequence 10 -> 11 -> 12. The writer lock
 * serializes publishers, so a separate atomic fetch-add is unnecessary.
 * Every access to these three shared words must still be atomic because
 * health_monitor_next_check() reads them without that lock.
 *
 * The odd marker and release fence precede the payload stores. If a reader
 * sees a payload store from this update, its acquire fence and final
 * sequence read must also account for this update's odd marker (or a
 * later version). It cannot accept that payload with the old version.
 * The final release store publishes the completed pair; an acquire load
 * observing that even version can safely read the preceding payload.
 *
 * These ordering rules matter on SMP: source-code order alone, or volatile
 * alone, is not a substitute for cross-CPU synchronization. Fences order
 * memory accesses; they do not wait for a reader or acquire a lock.
 */

static void health_monitor_publish(void)
{
	uint32_t sequence = __atomic_load_n(&g_health_sequence, __ATOMIC_RELAXED);
	uint32_t next = g_health_count ? g_health_heap[0].check_at : 0;

	/* Begin publication. Readers detecting an odd version return -EAGAIN. */

	__atomic_store_n(&g_health_sequence, sequence + 1, __ATOMIC_RELAXED);
	__atomic_thread_fence(__ATOMIC_RELEASE);

	/* Publish the pair. Count distinguishes an empty heap from a valid
	 * reservation at tick zero. RELAXED is sufficient for the individual
	 * words because the surrounding sequence/fences supply the ordering.
	 */

	__atomic_store_n(&g_health_next_count, g_health_count, __ATOMIC_RELAXED);
	__atomic_store_n(&g_health_next_tick, next, __ATOMIC_RELAXED);

	/* Commit the new version only after both payload words are ready. */

	__atomic_store_n(&g_health_sequence, sequence + 2, __ATOMIC_RELEASE);
}

/* Return whether lhs is an earlier reservation than rhs, in O(1).
 * Convert both unsigned ticks to signed distances from the same now:
 * overdue reservations have negative offsets, future ones nonnegative.
 * Equality returns false; the heap does not need a stable tie order.
 *
 * Use one now for the entire heap operation. Comparing lhs - rhs directly
 * can misorder an overdue reservation and a newly inserted INT32_MAX
 * timeout. Each key must remain within the supported half-range of now;
 * this is not a way to represent arbitrarily old overdue reservations.
 */

static bool health_monitor_earlier(uint32_t lhs, uint32_t rhs, uint32_t now)
{
	return (int32_t)(lhs - now) < (int32_t)(rhs - now);
}

/* Restore heap order by moving one entry toward the root, in O(log N).
 * The caller holds the registry lock and supplies an active index and a
 * fixed now. Only the entry's relation to its ancestors needs repair,
 * as after appending a new reservation or replacing a removed entry.
 *
 * Save the entry, move later parents down into its hole, then place it at
 * the first valid position. Do not change TCB state or publish the hint;
 * the enclosing registration/removal operation does that once finished.
 */

static void health_monitor_sift_up(unsigned int index, uint32_t now)
{
	struct health_monitor_entry_s entry = g_health_heap[index];
	unsigned int parent;

	while (index > 0) {
		parent = (index - 1) / 2;
		if (!health_monitor_earlier(entry.check_at, g_health_heap[parent].check_at, now)) {
			break;
		}

		g_health_heap[index] = g_health_heap[parent];
		index = parent;
	}

	g_health_heap[index] = entry;
}

/* Restore heap order by moving one entry toward the leaves, in O(log N).
 * The caller holds the registry lock; the child subtrees are already
 * ordered. Choose the earlier child at each level and move it up until
 * the saved entry can precede both children. Use the same now throughout.
 * Like sift_up, this changes only heap slots, not TCB state or the hint.
 */

static void health_monitor_sift_down(unsigned int index, uint32_t now)
{
	struct health_monitor_entry_s entry = g_health_heap[index];
	unsigned int child;

	while ((child = index * 2 + 1) < g_health_count) {
		if (child + 1 < g_health_count &&
			health_monitor_earlier(g_health_heap[child + 1].check_at, g_health_heap[child].check_at, now)) {
			child++;
		}

		if (!health_monitor_earlier(g_health_heap[child].check_at, entry.check_at, now)) {
			break;
		}

		g_health_heap[index] = g_health_heap[child];
		index = child;
	}

	g_health_heap[index] = entry;
}

/* Remove a known active heap slot, in O(log N), under the registry lock.
 * Fill the hole with the last entry, then repair upward if it precedes
 * its new parent, or downward otherwise. Removing the last entry itself
 * needs no repair; removing the sole entry leaves an empty heap.
 *
 * Clear the now-inactive tail so it retains no TCB pointer. This helper
 * does not free the TCB, clear its registration or publish the new root;
 * unregister owns those remaining steps. Preconditions: count > 0 and
 * index < count. The caller supplies the operation's fixed now.
 */

static void health_monitor_remove(unsigned int index, uint32_t now)
{
	g_health_count--;
	if (index < g_health_count) {
		g_health_heap[index] = g_health_heap[g_health_count];
		if (index > 0 &&
			health_monitor_earlier(g_health_heap[index].check_at,
				g_health_heap[(index - 1) / 2].check_at, now)) {
			health_monitor_sift_up(index, now);
		} else {
			health_monitor_sift_down(index, now);
		}
	}

	g_health_heap[g_health_count].tcb = NULL;
	g_health_heap[g_health_count].check_at = 0;
}

/* Shared STOP/cleanup implementation for an explicit, live target TCB.
 * The caller already holds the registry lock; do not lock recursively.
 * A registered TCB has timeout != 0 and exactly one heap entry.
 *
 * There is no per-TCB heap index, so find the target by pointer in O(N),
 * remove its entry, clear its state and publish the new earliest hint.
 * This deliberate linear search keeps the TCB small and KICK independent
 * of heap maintenance. An unregistered target returns -ENOENT without
 * changing the heap or publishing. Success returns OK; nothing is freed.
 */

static int health_monitor_unregister(FAR struct tcb_s *tcb)
{
	FAR struct health_monitor_s *state = health_monitor_state(tcb);
	unsigned int index;

	if (state->timeout == 0) {
		return -ENOENT;
	}

	for (index = 0; index < g_health_count; index++) {
		if (g_health_heap[index].tcb == tcb) {
			health_monitor_remove(index, (uint32_t)clock_systimer());
			break;
		}
	}

	state->timeout = 0;
	state->deadline = 0;
	health_monitor_publish();
	return OK;
}

/* Initialize a newly created task/pthread to the unregistered state, O(1).
 * task setup calls this after PID assignment but before the task can run.
 *
 * No heap entry exists yet, so there is nothing to remove or publish.
 * Never use this to reset an already registered TCB: it would leave its
 * old heap entry behind. Task restart must use cleanup instead.
 */

void health_monitor_task_init(FAR struct tcb_s *tcb)
{
	irqstate_t flags = health_monitor_lock();
	FAR struct health_monitor_s *state = health_monitor_state(tcb);

	/* A new TCB has an assigned PID but is not yet runnable or registered. */

	state->deadline = 0;
	state->timeout = 0;
	health_monitor_unlock(flags);
}

/* Register the calling thread, with O(log N) heap work and no allocation.
 * Convert milliseconds to supported ticks, obtain the current TCB before
 * entering the private lock, then check registration and static capacity.
 * With the lock held, initialize timeout/deadline, append a reservation at
 * that same deadline, restore heap order and publish the earliest hint.
 * No partially updated registration is exposed to other lock holders.
 *
 * Return OK on success, -EINVAL for an unsupported timeout, -EEXIST for
 * duplicate registration, or -ENOSPC for a full static heap. Failures do
 * not alter the registration. Call from thread context only; registration
 * by itself does not run the timeout inspector (a later implementation
 * stage connects it to the timer).
 */

int health_monitor_start(uint32_t timeout_ms)
{
	uint32_t timeout = health_monitor_timeout_ticks(timeout_ms);
	FAR struct health_monitor_s *state;
	FAR struct tcb_s *tcb;
	irqstate_t flags;
	uint32_t now;
	unsigned int index;
	int ret = OK;

	if (timeout == 0) {
		return -EINVAL;
	}

	tcb = this_task();
	flags = health_monitor_lock();
	state = health_monitor_state(tcb);
	if (state->timeout != 0) {
		ret = -EEXIST;
	} else if (g_health_count >= HEALTH_MONITOR_HEAP_CAPACITY) {
		ret = -ENOSPC;
	} else {
		now = (uint32_t)clock_systimer();
		state->timeout = timeout;
		state->deadline = now + timeout;
		index = g_health_count++;
		g_health_heap[index].tcb = tcb;
		g_health_heap[index].check_at = state->deadline;
		health_monitor_sift_up(index, now);
		health_monitor_publish();
	}

	health_monitor_unlock(flags);
	return ret;
}

/* Renew only the calling thread's actual deadline, with O(1) state work.
 * Under the registry lock, an active registration gets now + timeout;
 * an unregistered thread is a no-op. Do not reject an elapsed old deadline:
 * a KICK accepted before inspection is allowed to extend it.
 *
 * Neither the heap's check_at nor the published hint changes. They may
 * therefore request an earlier, harmless inspection. The timer stage
 * will re-read the actual deadline under the same lock when check_at is
 * due, and either reschedule that reservation or report expiry. This
 * lazy repair is what avoids O(log N) heap maintenance on every KICK.
 * Call from thread context; spinlock contention is separate from the
 * constant amount of state work described here.
 */

void health_monitor_kick(void)
{
	FAR struct tcb_s *tcb = this_task();
	irqstate_t flags = health_monitor_lock();
	FAR struct health_monitor_s *state = health_monitor_state(tcb);

	if (state->timeout != 0) {
		state->deadline = (uint32_t)clock_systimer() + state->timeout;
	}

	health_monitor_unlock(flags);
}

/* Unregister the calling thread from thread context. Obtain its TCB before
 * taking the private lock, then use the common O(N) unregister path.
 * Return OK if removed or -ENOENT if already unregistered. The thread and
 * its TCB remain alive; a later START can register them again.
 */

int health_monitor_stop(void)
{
	FAR struct tcb_s *tcb = this_task();
	irqstate_t flags = health_monitor_lock();
	int ret = health_monitor_unregister(tcb);

	health_monitor_unlock(flags);
	return ret;
}

/* Lifecycle counterpart of STOP: unregister the supplied target, which
 * may differ from the calling thread. Exit/restart/release paths call it
 * before PID reuse or TCB destruction so the heap retains no stale pointer.
 * NULL and already-unregistered targets are harmless; repeated calls on
 * the same live TCB are intentional and -ENOENT is ignored here.
 *
 * The caller must keep the target alive and prevent it from registering
 * again during teardown. This function protects registry data, not the
 * whole task lifecycle; it does not pause, delete or free the target.
 * Registered-target removal costs O(N); an unregistered target costs O(1).
 */

void health_monitor_cleanup(FAR struct tcb_s *tcb)
{
	irqstate_t flags;

	if (tcb == NULL) {
		return;
	}

	flags = health_monitor_lock();
	(void)health_monitor_unregister(tcb);
	health_monitor_unlock(flags);
}

/* Read the published earliest reservation with one O(1) snapshot attempt.
 * No spinlock, retry loop, heap access or TCB dereference is needed, making
 * this suitable for the future tick fast path and PM wakeup selection.
 * The caller supplies a valid output pointer.
 *
 * Read an even version, copy count/tick, then verify the version again.
 * Accept only the same even version at both ends. For example:
 *   10 -> read pair -> 10: accept the coherent pair;
 *   11 at entry:          writer active, return -EAGAIN;
 *   10 -> read pair -> 12: publication detected, return -EAGAIN.
 * A writer starting after our final check is allowed: this is a consistent
 * snapshot, not a promise that it remains the newest value afterward.
 * As with a finite version counter, one read attempt must not span a full
 * sequence cycle (2^31 publications, each advancing sequence by two).
 *
 * Return 1 and write check_at for a nonempty snapshot, 0 for an empty one,
 * or -EAGAIN when publication is detected during the attempt. Leave output
 * unchanged for 0/-EAGAIN. A successful check_at of zero is a valid tick,
 * not an empty marker; this is why count is part of the published pair.
 *
 * The returned time is only a scheduling hint, not proof of expiry. A due
 * hint still requires locked inspection of the actual heap/TCB deadline.
 * On -EAGAIN, a tick caller must defer rather than spin; PM must defer
 * sleep rather than treating the registry as empty. Those callers are
 * connected in later stages, not by this read function.
 */

int health_monitor_next_check(FAR uint32_t *check_at)
{
	uint32_t sequence;
	uint32_t count;
	uint32_t next;

	/* If we observe a completed writer's release store, ACQUIRE orders the
	 * following payload loads after that publication. Reject odd at once.
	 */

	sequence = __atomic_load_n(&g_health_sequence, __ATOMIC_ACQUIRE);
	if (sequence & 1) {
		return -EAGAIN;
	}

	/* Each load is atomic, but these two loads are not one atomic pair.
	 * The version check, not RELAXED alone, makes the pair usable.
	 */

	count = __atomic_load_n(&g_health_next_count, __ATOMIC_RELAXED);
	next = __atomic_load_n(&g_health_next_tick, __ATOMIC_RELAXED);

	/* Order the payload reads before the final version check. Together
	 * with the writer's release fence, seeing a newly written payload
	 * prevents us from accepting it under the old sequence value.
	 */

	__atomic_thread_fence(__ATOMIC_ACQUIRE);
	if (sequence != __atomic_load_n(&g_health_sequence, __ATOMIC_RELAXED)) {
		return -EAGAIN;
	}

	if (count == 0) {
		return 0;
	}

	*check_at = next;
	return 1;
}
