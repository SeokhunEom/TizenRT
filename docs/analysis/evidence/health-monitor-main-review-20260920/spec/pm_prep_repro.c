#define main original_pm_test_main
#include "/Volumes/T7/Dev/TizenRT/codex/260901-health-monitor/os/kernel/health_monitor/tests/pm_test.c"
#undef main

static unsigned int wall_tick;
static void slow_suspend(void)
{
    assert(g_irq_masked);
    wall_tick += 125;
}
static void wall_sleep(void)
{
    wall_tick += g_timer_us / 1000;
    g_missing_ticks = g_timer_us / 1000;
}
int main(void)
{
    __reset_test(100);
    wall_tick = 100;
    assert(health_monitor_start(100) == OK);
    g_on_suspend = slow_suspend;
    g_on_sleep = wall_sleep;
    pm_idle();
    printf("start=100 deadline=200 prep=125ms sleep_calls=%u sleep_ms=%u wall_now=%u os_now=%llu compensated=%llu\n",
        g_sleep_calls, g_timer_us / 1000, wall_tick, (unsigned long long)g_now,
        (unsigned long long)g_compensated_ticks);
    assert(g_sleep_calls == 1);
    assert(g_timer_us == 100000);
    assert(wall_tick == 325);
    assert(g_now == 200);
    return 0;
}
