#ifndef __MM_UMM_HEAP_UMM_MALLOC_H
#define __MM_UMM_HEAP_UMM_MALLOC_H

#include <stddef.h>
#include <tinyara/mm/mm.h>

FAR void *umm_malloc_with_caller(size_t size, mmaddress_t caller_retaddr);

#endif
