#ifndef __TINYARA_CLOCK_H
#define __TINYARA_CLOCK_H

#include <time.h>

#define TICK2USEC(tick) (tick)

static inline clock_t clock_systimer(void)
{
	return (clock_t)0;
}

#endif
