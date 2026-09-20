# Independent Spec review

Fixed main: 080481ee8bea3511ec5b0b8baac6d0a713f571aa
Fixed head: cc00205afc56e938132403dc3d3aae65db8e8cb6

## P2: PM final deadline selection still uses a frozen tick through suspend preparation

Spec: docs/HealthMonitorImplementationPlan.md:181 says "이미 검사할 시각이 됐다면 sleep 진입을 보류하고 timer 경로에서 검사하게 한다." Lines175 and179 also require sleep-time accounting and earliest reservation selection; docs/health-monitor/05-pm-wakeup.md:55 claims the final check after device/CPU suspension makes that selection current.

New integration source: os/pm/pm_idle.c:312 computes remaining from clock_systimer(); the recheck at507 occurs after suspend_devices()/disable_secondary_cpus(), while pm_idle() masked CPU0 IRQs at579. CPU0 is the system tick writer. Reference-board sleep accounting only starts its timestamp within tizenrt_sleep_processing() at os/board/rtl8730e/src/component/soc/amebad2/misc/ameba_tizenrt_pmu.c:210 and reports the interval at224-232. os/arch/arm/src/amebasmart/amebasmart_timerisr.c:118 resets the timer comparator on resume. Thus time spent preparing sleep is neither subtracted from the new HM wakeup delay nor subsequently made up by queued timer ticks.

Bounded native host reproduction includes the unmodified production PM/Health Monitor implementation and existing test fixtures. At OS tick100, START(100) gives deadline200. A suspend callback advances independent hardware time125ms while IRQs remain masked. The final recheck still permits100ms sleep; wakeup has wall time325 but OS time200. A second test including the real RTL watchdog port and vendor register model with5sWDT also permits the same sleep without reset. These are controlled model/runtime observations plus static board-call mapping, not physical board results.

Reproduce: sh /private/tmp/hm-main-review-7174_ji6/spec/pm-repro.sh
Output: /private/tmp/hm-main-review-7174_ji6/spec/pm-repro.log

start=100 deadline=200 prep=125ms sleep_calls=1 sleep_ms=100 wall_now=325 os_now=200 compensated=100
deadline=200 prep=125ms sleep_calls=1 sleep_ms=100 elapsed_hw_ms=224 os_now=200 reset=0 wdt_started=1

The224ms hardware line uses32768Hz integer truncation. Both builds used -Wall -Wextra -Werror and ASan/UBSan and exited0; assertions confirm the unexpected sleep rather than claim a passing product requirement.

Attribution: The reference board's missed preparation accounting predates this feature. The new HM PM integration relies on that unsatisfied accounting assumption, so report it as an integration gap, not a newly introduced standalone PM timer bug. This is not a demand for a fixed detection-latency threshold. It concerns authorizing additional sleep after the registered timeout has already elapsed during sleep preparation. Fix direction: preserve a monotonic elapsed-time basis across the whole IRQ-masked PM transition; include that interval in final HM selection and compensation without double-counting actual sleep.

## Other reviewed contracts

No additional confirmed Spec violation found in current heap ordering/half-range policy, owner-based START/KICK/STOP, lazy heap repair, cleanup-before-reuse hooks, lock order/one-shot ISR trylock, atomic pointer-free hint, expiry-after-unlock behavior, IRQ-WDT ownership/feed condition, disabled feature branches and config exclusions. Deliberately omitted production expired-task identity and unlimited due-candidate count are agreed policy, not Spec bugs. CPU-fault-to-recovery and unload validation exclusions were respected. SMP cache behavior, physical PM and watchdog timing remain unverified by host/QEMU proof.
