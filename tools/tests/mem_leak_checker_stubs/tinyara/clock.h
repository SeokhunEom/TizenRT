#ifndef __TEST_TINYARA_CLOCK_H
#define __TEST_TINYARA_CLOCK_H

#include <stdint.h>

typedef unsigned long clock_t;

#define TICK2USEC(value) ((uint64_t)(value))

clock_t clock_systimer(void);

#endif
