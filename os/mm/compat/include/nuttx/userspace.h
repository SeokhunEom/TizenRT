/* SPDX-License-Identifier: Apache-2.0 */
#ifndef TIZENRT_NUTTX_MM_USERSPACE_H
#define TIZENRT_NUTTX_MM_USERSPACE_H
/* Only used by MM_INTERNAL_HEAP's optional allocation-failure diagnostic.
 * The bridge explicitly registers the TizenRT kernel/user backend pointers. */
struct mm_heap_s;
extern struct mm_heap_s *g_mmheap;
struct trmm_userspace_data_s { struct mm_heap_s **us_heap; };
struct trmm_userspace_s { const struct trmm_userspace_data_s *us_data; };
extern const struct trmm_userspace_s trmm_userspace;
#define USERSPACE (&trmm_userspace)
#endif
