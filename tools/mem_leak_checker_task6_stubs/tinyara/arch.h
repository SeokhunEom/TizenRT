#ifndef __TINYARA_ARCH_H
#define __TINYARA_ARCH_H

#include <stdint.h>

#include <tinyara/clock.h>

static inline uint64_t up_mem_leak_monotonic_usec(void)
{
	return (uint64_t)TICK2USEC(clock_systimer());
}

#endif
