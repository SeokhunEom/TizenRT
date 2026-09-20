# Spec follow-up review: Health Monitor P2 fixes

Reviewed the working-tree product diff against HEAD cc00205afc56e938132403dc3d3aae65db8e8cb6. Read-only review; no product edits, staging or commits by this reviewer.

## F1: PM transition time

The modified design addresses the previous frozen-clock gap. RTL up_timer_disable captures pending ARM timer phase and an AON snapshot; get_elapsedtick supplies a cumulative interval; final wakeup selection subtracts that interval from Health Monitor and software watchdog delays. The hardware watchdog already measures elapsed hardware time and is not charged twice. Successful sleep and transition aborts converge on compensation after CPU/device resume. The last sample and fractional tick are used by up_timer_enable to maintain the next tick boundary. The Health Monitor heap, single lock and published scheduling hint remain unchanged.

Source reviewed: os/pm/pm_idle.c:238-274,294-345,490-563; os/arch/arm/src/amebasmart/amebasmart_timerisr.c:116-158; os/arch/arm/src/amebasmart/amebasmart_idle.c:51-57; os/include/tinyara/pm/pm.h:241-258.

One additional corner was found during review: get_elapsedtick was enabled for RTL PM_TICKSUPPRESS even with HEALTH_MONITOR and WATCHDOG_FOR_IRQ both off, but the final delay calculation was still conditionally omitted for that case. An independent actual-PM host fixture reproduced25ms preparation plus100ms software-timer sleep, yielding OS225 from OS100. The executor removed that condition, and the same independent fixture now confirms75ms sleep and OS200. This finding is resolved in the reviewed working tree.

Evidence: spec_pm_off_repro.c and spec_pm_off_repro.log in this directory. Native Clang build with -Wall -Wextra -Werror, ASan/UBSan; exit0. Latest output:

health_off irq_wdog_off prepare=25 sw_delay=100 sleep_ms=75 now=200

This is host-model evidence with unmodified production pm_idle.c, not a physical RTL PM measurement. The executor is separately creating the actual RTL timer-source fixture and target compilation. Important final checks: pending whole/fractional ticks before disable, PG ARM-counter restart, AON32-bit wrap, elapsed time between final sample and timer-enable, and repeated abort fractional phase. Their final outcomes are not asserted here.

## F2: timeout diagnosis

The revised implementation snapshots scalar PID/deadline while the registry lock still protects the TCB, releases the lock at health_monitor.c:594, records reason62, then emits the single low-level diagnostic at604-605 and enters PANIC. It does not dereference the inspected TCB after unlock, add a new lock, allocate, change expiration policy, or expand normal-tick/KICK work. Existing DEBUG_ERROR gating is preserved. The updated spec permits this minimal timeout identity record, while additional stacks/history remain outside scope.

## Result

No remaining concrete Spec defect found in the reviewed F1/F2 code after the software-watchdog-only final recheck correction. This does not certify actual hardware timing, SMP power/cache behavior, or pending target tests. Mid-transition registration policy is being independently assessed by the Standards reviewer and should be resolved before final approval if an executable path is found.
