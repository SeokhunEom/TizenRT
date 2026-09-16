/* SPDX-License-Identifier: Apache-2.0 */
/* Real PM/registry, port, vendor WDT register operations and reboot decoder.
 * The hardware clock, MMIO storage and counting/reset model are host fixtures.
 */
#define CONFIG_WATCHDOG 1
#define CONFIG_WATCHDOG_FOR_IRQ 1
#ifndef HEALTH_MONITOR_TEST_NO_WDOG_WAKEUP
#define CONFIG_ARCH_HAVE_WDOG_WAKEUP 1
#endif
#ifndef CONFIG_WATCHDOG_FOR_IRQ_INTERVAL
#define CONFIG_WATCHDOG_FOR_IRQ_INTERVAL 5000
#endif
#define CONFIG_SYSTEM_REBOOT_REASON 1
#define ARM_CORE_CA32 1
#define MBED_OBJECTS_H /* Unused GPIO/serial object types are target-specific. */

#define main pm_regression_main
#include "pm_test.c"
#undef main
#include "ameba_soc.h"

static void __record_start(void);
static void __record_refresh(void);
#define watchdog_start __record_start
#define watchdog_refresh __record_refresh
#include "../../../arch/arm/src/amebasmart/amebasmart_watchdog_lowerhalf.c"
#undef watchdog_start
#undef watchdog_refresh
#include "../../../board/rtl8730e/src/component/mbed/targets/hal/rtl8730e/wdt_api.c"
/* The ROM supplies a weak default initializer; this board overrides it. */
#define WDG_Init test_rom_wdg_init
#include "../../../board/rtl8730e/src/component/soc/amebad2/fwlib/rom_common/ameba_wdg.c"
#undef WDG_Init
#include "../../../arch/arm/src/amebasmart/amebasmart_reboot_reason.c"

WDG_TypeDef g_test_wdg[5];
static uint32_t g_hw_ticks;
static uint32_t g_reload_tick;
static unsigned int g_hw_refreshes;
static unsigned int g_hw_starts;
static unsigned int g_test_cpu;
static uint32_t g_backup[3];
static uint32_t g_boot_reason;

uint32_t SYSTIMER_TickGet(void)
{
	return g_hw_ticks;
}

unsigned int up_cpu_index(void)
{
	return g_test_cpu;
}

void DelayUs(uint32_t usec)
{
	(void)usec;
	assert(false); /* No simulated register update is busy in these tests. */
}

void InterruptRegister(IRQ_FUN handler, IRQn_Type irq, uint32_t data, int priority)
{
	(void)handler;
	(void)irq;
	(void)data;
	(void)priority;
	assert(false); /* The IRQ watchdog uses hardware reset-only mode. */
}

void InterruptEn(IRQn_Type irq, int priority)
{
	(void)irq;
	(void)priority;
	assert(false);
}

uint32_t BKUP_Read(unsigned int reg)
{
	assert(reg < 3);
	return g_backup[reg];
}

void BKUP_Write(unsigned int reg, uint32_t value)
{
	assert(reg < 3);
	g_backup[reg] = value;
}

uint32_t BOOT_Reason(void)
{
	return g_boot_reason;
}

static void __record_start(void)
{
	watchdog_start();
	assert(WDG4_DEV->WDG_MKEYR == WDG_FUNC_EN);
	g_reload_tick = g_hw_ticks;
	g_hw_starts++;
}

static void __record_refresh(void)
{
	watchdog_refresh();
	assert(WDG4_DEV->WDG_MKEYR == WDG_REFRESH);
	g_reload_tick = g_hw_ticks;
	g_hw_refreshes++;
}

static void __advance_hardware(uint32_t ms)
{
	g_hw_ticks += ((uint64_t)ms * 32768) / 1000;
	/* Continuous-count model, including sleep. Registers were programmed by
	 * real vendor code; this arithmetic is not proof of physical HW timing.
	 */
	uint32_t period = (WDG_GET_RELOAD(WDG4_DEV->WDG_RLR) + 1) *
		(WDG_GET_PRER(WDG4_DEV->WDG_RLR) + 1);
	if (g_hw_starts && (uint32_t)(g_hw_ticks - g_reload_tick) >= period) {
		g_boot_reason = AON_BIT_RSTF_WDG4;
	}
}

static void __reset_watchdog(void)
{
	__reset_test(100);
	memset(g_test_wdg, 0, sizeof(g_test_wdg));
	memset(g_backup, 0, sizeof(g_backup));
	g_backup[BKUP_REG1] = REBOOT_REASON_INITIALIZED;
	g_irq_wdog_started = false;
	g_hw_ticks = g_reload_tick = 0;
	g_hw_refreshes = g_hw_starts = g_test_cpu = g_boot_reason = 0;
	up_wdog_init(CONFIG_WATCHDOG_FOR_IRQ_INTERVAL);
}

static void __test_start_and_ownership(void)
{
	assert(up_wdog_getwakeupdelay() == 0);
	up_wdog_keepalive();
	assert(g_hw_refreshes == 0);
	__reset_watchdog();
	assert(g_hw_starts == 1 && g_irq_wdog_started);
	assert(WDG_GET_PRER(WDG4_DEV->WDG_RLR) == 0x5d);
	assert(WDG_GET_RELOAD(WDG4_DEV->WDG_RLR) == 5 * 348);
	assert((WDG4_DEV->WDG_CR & WDG_BIT_EIE) == 0);
	assert(WDG4_DEV->WDG_WINR == 0xffff);
	assert(up_wdog_getwakeupdelay() == 2500);
	assert(amebasmart_wdg_initialize("/dev/watchdog0", 1000) == -EBUSY);
	up_wdog_init(CONFIG_WATCHDOG_FOR_IRQ_INTERVAL);
	up_watchdog_disable();
	assert(g_hw_starts == 1 && g_hw_refreshes == 0);
	assert(WDG_GET_RELOAD(WDG4_DEV->WDG_RLR) == 5 * 348);
#ifdef CONFIG_SMP
	g_test_cpu = 1;
	up_wdog_keepalive();
	assert(g_hw_refreshes == 0);
	g_test_cpu = 0;
#endif
	up_wdog_keepalive();
	assert(g_hw_refreshes == 1);
}

static void __test_wakeup_budget(void)
{
	__reset_watchdog();
	__advance_hardware(2000);
	g_prepare_ticks += 2000;
	assert(up_wdog_getwakeupdelay() == 500);
	assert(g_now == 100); /* Budget advances even with the system tick masked. */
	__advance_hardware(501);
	assert(up_wdog_getwakeupdelay() == -EAGAIN);
	assert(g_hw_refreshes == 0);
	up_wdog_keepalive();
	assert(up_wdog_getwakeupdelay() == 2500);

	g_hw_ticks = UINT32_MAX - 32767;
	up_wdog_keepalive();
	__advance_hardware(1000);
	assert(up_wdog_getwakeupdelay() == 1500);
}

#ifdef CONFIG_PM_TIMEDWAKEUP
static void __sleep_until_timer(void)
{
	assert(g_timer_us > 0 && g_timer_us <= 2500000);
	g_missing_ticks = g_timer_us / 1000;
	__advance_hardware(g_timer_us / 1000);
}

static void __slow_suspend(void)
{
	__advance_hardware(2000);
	g_prepare_ticks += 2000;
}

static void __too_slow_suspend(void)
{
	__advance_hardware(2501);
	g_prepare_ticks += 2501;
}
#endif

static void __test_sleep_and_stall(void)
{
	__reset_watchdog();
#ifdef CONFIG_PM_TIMEDWAKEUP
	g_on_sleep = __sleep_until_timer;
	__run_pm(true, 2500000); /* No SW reservation: HW watchdog still wakes PM. */
	assert(g_now == 2600 && g_hw_refreshes == 0 && g_boot_reason == 0);
	up_wdog_keepalive();
	__advance_hardware(100);
	assert(g_boot_reason == 0);

	__reset_watchdog();
	assert(health_monitor_start(1000) == OK);
	g_on_sleep = __sleep_until_timer;
	__run_pm(true, 1000000);
	assert(g_task.health_monitor.deadline == 1100 && g_now == 1100);
	assert(g_hw_refreshes == 0 && g_boot_reason == 0);

	for (unsigned int wdog = 1000; wdog <= 4000; wdog += 3000) {
		__reset_watchdog();
		assert(health_monitor_start(3000) == OK);
		g_wdog_delay = wdog;
		g_on_sleep = __sleep_until_timer;
		__run_pm(true, wdog < 2500 ? wdog * 1000 : 2500000);
		assert(g_hw_refreshes == 0 && g_boot_reason == 0);
	}

	__reset_watchdog();
	g_ops.set_timer = NULL; /* HW-only wakeup must validate board capability. */
	__run_pm(false, 0);
	assert(g_suspend_calls == 0);

	__reset_watchdog();
	g_on_suspend = __slow_suspend;
	g_on_sleep = __sleep_until_timer;
	__run_pm(true, 500000);
	assert(g_boot_reason == 0);

	__reset_watchdog();
	g_on_suspend = __too_slow_suspend;
	__run_pm(false, 0);
	assert(g_missing_reads == 0 && g_now == 2601);

	__reset_watchdog();
	__advance_hardware(2492);
	__run_pm(false, 0); /* Also apply the existing 10-tick PM threshold. */
#else
	__run_pm(false, 0); /* Unstoppable WDT forbids sleep without timed wakeup. */
#endif
	__reset_watchdog();
	__advance_hardware(5100); /* No CPU0 progress/keepalive. */
	assert(g_boot_reason == AON_BIT_RSTF_WDG4);
	up_reboot_reason_init();
	assert(up_reboot_reason_read() == REBOOT_SYSTEM_WATCHDOG);

	__reset_watchdog();
	assert(health_monitor_start(10) == OK);
	g_now = 110;
	__inspect(true); /* SW verdict writes reason 62 before PANIC. */
	assert(g_backup[BKUP_REG1] == REBOOT_SYSTEM_HEALTH_MONITOR_TIMEOUT);
	assert(g_hw_refreshes == 0);
	__advance_hardware(5100); /* Model PANIC stalled until hardware fallback. */
	assert(g_boot_reason == AON_BIT_RSTF_WDG4);
	up_reboot_reason_init();
	assert(up_reboot_reason_read() == REBOOT_SYSTEM_HEALTH_MONITOR_TIMEOUT);
}

int main(void)
{
	__test_start_and_ownership();
	__test_wakeup_budget();
	__test_sleep_and_stall();
	puts("PASS: real WDT start/registers, exclusive owner, PM budget and reboot cause policy");
	return 0;
}
