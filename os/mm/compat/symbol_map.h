/* SPDX-License-Identifier: Apache-2.0 */
/* Force-include only while compiling imported mm_heap and its bridge. */
#ifndef TIZENRT_NUTTX_MM_SYMBOL_MAP_H
#define TIZENRT_NUTTX_MM_SYMBOL_MAP_H
#include <stdlib.h>
#include <string.h>
/* NuttX 12.13.0 realloc moves overlapping chunks downward using memcpy.
 * Preserve source bytes while supplying defined overlap-safe semantics. */
#define memcpy memmove
#define mallinfo nxmm_mallinfo_info
#define malltask nxmm_malltask
#define mallinfo_task nxmm_mallinfo_task_info
#define mm_heap_s nxmm_mm_heap_s
#define mm_allocnode_s nxmm_mm_allocnode_s
#define mm_freenode_s nxmm_mm_freenode_s
#define mm_delaynode_s nxmm_mm_delaynode_s
#define mm_heap_config_s nxmm_mm_heap_config_s
#define mm_initialize nxmm_mm_initialize
#define mm_initialize_heap nxmm_mm_initialize_heap
#define mm_addregion nxmm_mm_addregion
#define mm_uninitialize nxmm_mm_uninitialize
#define mm_malloc nxmm_mm_malloc
#define mm_malloc_size nxmm_mm_malloc_size
#define mm_free nxmm_mm_free
#define mm_delayfree nxmm_mm_delayfree
#define mm_realloc nxmm_mm_realloc
#define mm_calloc nxmm_mm_calloc
#define mm_zalloc nxmm_mm_zalloc
#define mm_memalign nxmm_mm_memalign
#define mm_heapmember nxmm_mm_heapmember
#define mm_brkaddr nxmm_mm_brkaddr
#define mm_extend nxmm_mm_extend
#define mm_mallinfo nxmm_mm_mallinfo
#define mm_mallinfo_task nxmm_mm_mallinfo_task
#define mm_heapfree nxmm_mm_heapfree
#define mm_heapfree_largest nxmm_mm_heapfree_largest
#define mm_checkcorruption nxmm_mm_checkcorruption
#define mm_foreach nxmm_mm_foreach
#define mm_lock nxmm_mm_lock
#define mm_unlock nxmm_mm_unlock
#define mm_lock_irq nxmm_mm_lock_irq
#define mm_unlock_irq nxmm_mm_unlock_irq
#define mm_shrinkchunk nxmm_mm_shrinkchunk
#define mm_free_delaylist nxmm_mm_free_delaylist
#define mm_memdump nxmm_mm_memdump
#define mm_dump_handler nxmm_mm_dump_handler
#define mm_mempool_dump_handle nxmm_mm_mempool_dump_handle
#define g_mmheap nxmm_g_mmheap
#define g_kmmheap nxmm_g_kmmheap
#define g_mm_seqno nxmm_g_mm_seqno
#endif
