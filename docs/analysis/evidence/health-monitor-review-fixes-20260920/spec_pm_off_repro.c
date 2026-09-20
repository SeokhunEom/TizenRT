#define HEALTH_MONITOR_TEST_OFF 1
#define main original_pm_main
#include "/Volumes/T7/Dev/TizenRT/codex/260901-health-monitor/os/kernel/health_monitor/tests/pm_test.c"
#undef main
static void prep25(void) { g_prepare_ticks = 25; }
static void sleep_timer(void) { g_missing_ticks = g_timer_us / 1000; }
int main(void)
{
    __reset_test(100);
    g_wdog_delay = 100;
    g_on_suspend = prep25;
    g_on_sleep = sleep_timer;
    pm_idle();
    printf("health_off irq_wdog_off prepare=25 sw_delay=100 sleep_ms=%u now=%llu\n", g_timer_us/1000, (unsigned long long)g_now);
    assert(g_timer_us == 75000 && g_now == 200);
    return 0;
}
