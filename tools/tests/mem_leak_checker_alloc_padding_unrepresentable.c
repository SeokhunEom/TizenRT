#include <stddef.h>
#include <stdint.h>

#include "../../os/include/tinyara/mm/mm_alloc_padding.h"

_Static_assert(MM_ALLOC_PADDING_BOUND(1, ((size_t)UINT16_MAX / 2) + 2) <= UINT16_MAX,
		"allocation padding bound exceeds uint16_t");

int main(void) { return 0; }
