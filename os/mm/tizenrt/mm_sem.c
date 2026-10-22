/****************************************************************************
 *
 * Copyright 2016 Samsung Electronics All Rights Reserved.
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
 * mm/mm_heap/mm_sem.c
 *
 *   Copyright (C) 2007-2009, 2013 Gregory Nutt. All rights reserved.
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
#include <debug.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>
#include <sched.h>

#include <tinyara/mm/mm.h>
#include <tinyara/sched.h>
#include <tinyara/arch.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* Required operations still execute when assertions are disabled. */
static void mm_require(bool condition)
{
    if (!condition) { PANIC(); for (;;) {} }
}

/* task_exit changes the ready head before the next context is running. Do
 * not obtain a heap mutex or touch that next task's cancellation state there. */
static bool mm_safe_context(void)
{
#if defined(CONFIG_BUILD_FLAT) || defined(__KERNEL__)
    struct tcb_s *tcb = sched_self();
    return !up_interrupt_context() && (!tcb || tcb->task_state == TSTATE_TASK_RUNNING);
#else
    return true;
#endif
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: mm_seminitialize
 *
 * Description:
 *   Initialize the MM mutex
 *
 ****************************************************************************/

void mm_seminitialize(FAR struct mm_heap_s *heap)
{
	/* Initialize the MM semaphore to one (to support one-at-a-time access to
	 * private data sets.
	 */

	mm_require(sem_init(&heap->mm_semaphore, 0, 1) == OK);

	heap->mm_holder      = -1;
	heap->mm_counts_held = 0;
	heap->mm_cancel_state = TASK_CANCEL_ENABLE;
}

/****************************************************************************
 * Name: mm_trysemaphore
 *
 * Description:
 *   Try to take the MM mutex.  This is called only from the OS in certain
 *   conditions when it is necessary to have exclusive access to the memory
 *   manager but it is impossible to wait on a semaphore (e.g., the idle
 *   process when it performs its background memory cleanup).
 *
 ****************************************************************************/

int mm_trysemaphore(FAR struct mm_heap_s *heap)
{
    pid_t pid;
    int saved_state;
    int saved_errno;
    int ret;
    if (!mm_safe_context()) { return -EAGAIN; }
    pid = getpid();
    if (heap->mm_holder == pid && heap->mm_counts_held) {
        heap->mm_counts_held++;
        return OK;
    }
    saved_errno = get_errno();
    mm_require(task_setcancelstate(TASK_CANCEL_DISABLE, &saved_state) == OK);
    ret = sem_trywait(&heap->mm_semaphore);
    if (ret != OK) {
        ret = -get_errno();
        set_errno(saved_errno);
        mm_require(task_setcancelstate(saved_state, NULL) == OK);
        return ret;
    }
    heap->mm_holder = pid;
    heap->mm_counts_held = 1;
    heap->mm_cancel_state = saved_state;
    set_errno(saved_errno);
    return OK;
}

/****************************************************************************
 * Name: mm_takesemaphore
 *
 * Description:
 *   Take the MM mutex.  This is the normal action before all memory
 *   management actions.
 *
 ****************************************************************************/

bool mm_takesemaphore(FAR struct mm_heap_s *heap)
{
    pid_t pid;
    int saved_state;
    int saved_errno;
    int ret;
    if (!mm_safe_context()) { return false; }
    pid = getpid();
    if (heap->mm_holder == pid && heap->mm_counts_held) {
        heap->mm_counts_held++;
        return true;
    }
    /* The core mutex is nested inside this lock. It must inherit DISABLE so
     * restoring cancellation at its unlock cannot abandon metadata locked. */
    saved_errno = get_errno();
    mm_require(task_setcancelstate(TASK_CANCEL_DISABLE, &saved_state) == OK);
    do {
        ret = sem_wait(&heap->mm_semaphore);
    } while (ret != OK && get_errno() == EINTR);
    mm_require(ret == OK);
    heap->mm_holder = pid;
    heap->mm_counts_held = 1;
    heap->mm_cancel_state = saved_state;
    set_errno(saved_errno);
    return true;
}

/****************************************************************************
 * Name: mm_givesemaphore
 *
 * Description:
 *   Release the MM mutex when it is not longer needed.
 *
 ****************************************************************************/

void mm_givesemaphore(FAR struct mm_heap_s *heap)
{
    int saved_state;
    int saved_errno = get_errno();
    mm_require(heap->mm_holder == getpid() && heap->mm_counts_held > 0);
    if (--heap->mm_counts_held) { return; }
    saved_state = heap->mm_cancel_state;
    heap->mm_holder = -1;
    mm_require(sem_post(&heap->mm_semaphore) == OK);
    set_errno(saved_errno);
    mm_require(task_setcancelstate(saved_state, NULL) == OK);
}

/****************************************************************************
 * Name: mm_is_sem_available
 *
 * Description:
 *   Check availability of mm semaphore 
 *
 ****************************************************************************/
void mm_is_sem_available(void *address)
{
	struct mm_heap_s *heap;

	heap = mm_get_heap(address);
	if (heap == NULL) {
		mdbg("Invalid Heap address given, Fail to check sem availability.\n");
		return;
	}
	mm_require(mm_takesemaphore(heap));
	mm_givesemaphore(heap);
}
