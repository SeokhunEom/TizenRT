/* SPDX-License-Identifier: Apache-2.0 */
/* Register storage and external hardware primitives; real vendor WDG macros. */
#ifndef HEALTH_MONITOR_TEST_AMEBA_SOC_H
#define HEALTH_MONITOR_TEST_AMEBA_SOC_H
#include <assert.h>
#include <stdint.h>
typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t u8;
typedef int IRQn_Type;
typedef u32 (*IRQ_FUN)(void *data);
#define __IO volatile
#undef __weak
#define __weak
#define __WEAK
#define _LONG_CALL_
#define HAL_ROM_TEXT_SECTION
#define ENABLE 1
#define DISABLE 0
#define assert_param(condition) assert(condition)
#define dbg(...) ((void)0)
#define INT_PRI_HIGH 1
#define WDG4_IRQ 62
#include <ameba_wdg.h>
void WDG_Wait_Busy(WDG_TypeDef *wdg);
#include <sysreg_aon.h>
extern WDG_TypeDef g_test_wdg[5];
#define IWDG_DEV (&g_test_wdg[0])
#define WDG1_DEV (&g_test_wdg[1])
#define WDG2_DEV (&g_test_wdg[2])
#define WDG3_DEV (&g_test_wdg[3])
#define WDG4_DEV (&g_test_wdg[4])
#define BKUP_REG1 1
#define BKUP_REG2 2
uint32_t SYSTIMER_TickGet(void);
void DelayUs(uint32_t usec);
void InterruptRegister(IRQ_FUN handler, IRQn_Type irq, uint32_t data, int priority);
void InterruptEn(IRQn_Type irq, int priority);
uint32_t BKUP_Read(unsigned int reg);
void BKUP_Write(unsigned int reg, uint32_t value);
uint32_t BOOT_Reason(void);
#endif
