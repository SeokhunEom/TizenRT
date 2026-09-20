#include "watchdog_fixture.inc"
static void slow_hm_suspend(void)
{
    assert(g_irq_masked);
    __advance_hardware(125);
}
int main(void)
{
    __reset_watchdog();
    assert(health_monitor_start(100) == OK);
    g_on_suspend = slow_hm_suspend;
    g_on_sleep = __sleep_until_timer;
    pm_idle();
    printf("deadline=200 prep=125ms sleep_calls=%u sleep_ms=%u elapsed_hw_ms=%llu os_now=%llu reset=%u wdt_started=%u\n", g_sleep_calls, g_timer_us/1000, (unsigned long long)g_hw_ticks*1000/32768, (unsigned long long)g_now, g_boot_reason, g_hw_starts);
    assert(g_sleep_calls == 1 && g_timer_us == 100000);
    assert(g_now == 200 && g_boot_reason == 0 && g_hw_starts == 1);
    return 0;
}
