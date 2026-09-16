/* SPDX-License-Identifier: Apache-2.0 */
#ifndef HEALTH_MONITOR_TEST_WDT_API_H
#define HEALTH_MONITOR_TEST_WDT_API_H
#include "ameba_soc.h"
typedef u32 (*wdt_irq_handler)(void *id);
void watchdog_init(uint32_t timeout_ms);
void watchdog_start(void);
void watchdog_stop(void);
void watchdog_refresh(void);
void watchdog_irq_init(wdt_irq_handler handler, uint32_t id);
#endif
