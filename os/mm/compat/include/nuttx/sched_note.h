/* SPDX-License-Identifier: Apache-2.0 */
#ifndef TIZENRT_NUTTX_MM_SCHED_NOTE_H
#define TIZENRT_NUTTX_MM_SCHED_NOTE_H
#if defined(CONFIG_SCHED_INSTRUMENTATION_HEAP)
#error "NuttX heap trace provider has not been ported"
#endif
/* Matches upstream's disabled scheduler-instrumentation interface. */
#define sched_note_heap(type, heap, mem, size, used) ((void)0)
#endif
