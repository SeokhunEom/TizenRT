#ifndef __MLC_TASK6_NATIVE_IRQ_IRQ_H
#define __MLC_TASK6_NATIVE_IRQ_IRQ_H

#include <stdbool.h>

bool up_cpu_pausereq(int cpu);
void up_cpu_paused_save(void);
bool up_cpu_paused(int cpu);
void up_cpu_paused_restore(void);

#endif
