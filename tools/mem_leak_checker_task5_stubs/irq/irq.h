#ifndef __IRQ_IRQ_H
#define __IRQ_IRQ_H

#include <stdbool.h>

bool up_cpu_pausereq(int cpu);
bool up_cpu_hotplugreq(int cpu);
void up_cpu_paused_save(void);
bool up_cpu_paused(int cpu);
void up_cpu_paused_restore(void);
bool up_cpu_hotplugabort(int cpu);

#endif
