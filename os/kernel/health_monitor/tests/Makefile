# SPDX-License-Identifier: Apache-2.0
# Host-only tests. OUT_DIR keeps all generated files outside the source tree.
HOSTCC ?= cc
TEST_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
OS_DIR := $(abspath $(TEST_DIR)/../../..)
OUT_DIR ?= /tmp/tizenrt-health-monitor-tests
SANITIZERS ?= -fsanitize=address,undefined -fno-sanitize-recover=all
TEST_FLAGS = -std=gnu11 -O1 -g -Wall -Wextra -Werror -pthread $(SANITIZERS)
TEST_INCLUDES = -I$(TEST_DIR)/include -I$(OS_DIR)/kernel -idirafter $(OS_DIR)/include
TEST_DEPS = $(TEST_DIR)/registry_test.c $(wildcard $(TEST_DIR)/include/tinyara/*.h) \
	$(TEST_DIR)/include/assert.h $(TEST_DIR)/include/debug.h \
	$(TEST_DIR)/include/sched/sched.h $(TEST_DIR)/../health_monitor.c \
	$(TEST_DIR)/../health_monitor.h $(OS_DIR)/include/tinyara/health_monitor.h
DRIVER_INCLUDES = -I$(TEST_DIR)/driver_include $(TEST_INCLUDES)
DRIVER_DEPS = $(TEST_DEPS) $(TEST_DIR)/driver_test.c \
	$(wildcard $(TEST_DIR)/driver_include/*.h $(TEST_DIR)/driver_include/inode/*.h \
	$(TEST_DIR)/driver_include/sys/*.h \
	$(TEST_DIR)/driver_include/tinyara/fs/*.h $(TEST_DIR)/driver_include/driver/block/*.h) \
	$(OS_DIR)/drivers/health_monitor.c $(OS_DIR)/include/fcntl.h \
	$(OS_DIR)/fs/vfs/fs_open.c $(OS_DIR)/fs/vfs/fs_close.c \
	$(OS_DIR)/fs/vfs/fs_ioctl.c $(OS_DIR)/fs/vfs/fs_getfilep.c \
	$(OS_DIR)/fs/inode/fs_fileclose.c
TIMER_INCLUDES = -I$(TEST_DIR)/timer_include $(TEST_INCLUDES)
TIMER_DEPS = $(TEST_DEPS) $(TEST_DIR)/timer_test.c \
	$(wildcard $(TEST_DIR)/timer_include/*/*.h) \
	$(OS_DIR)/kernel/sched/sched_processtimer.c \
	$(OS_DIR)/arch/arm/src/common/up_reboot_reason.c \
	$(OS_DIR)/include/tinyara/reboot_reason.h
PM_INCLUDES = -I$(TEST_DIR)/pm_include $(TIMER_INCLUDES)
PM_DEPS = $(TEST_DEPS) $(TEST_DIR)/pm_test.c $(OS_DIR)/pm/pm_idle.c \
	$(wildcard $(TEST_DIR)/pm_include/tinyara/*.h $(TEST_DIR)/pm_include/tinyara/pm/*.h) \
	$(TEST_DIR)/timer_include/tinyara/arch.h
PM_TESTS = $(addprefix $(OUT_DIR)/,pm-smp pm-up pm-no-timed-wakeup pm-no-tick-suppress pm-off)
RTL_DIR = $(OS_DIR)/board/rtl8730e/src/component
WDOG_INCLUDES = -I$(TEST_DIR)/wdog_include $(PM_INCLUDES) -I$(RTL_DIR)/soc/amebad2/fwlib/include
WDOG_DEPS = $(PM_DEPS) $(TEST_DIR)/watchdog_test.c \
	$(wildcard $(TEST_DIR)/wdog_include/*.h $(TEST_DIR)/wdog_include/tinyara/*.h) \
	$(OS_DIR)/arch/arm/src/amebasmart/amebasmart_watchdog_lowerhalf.c \
	$(OS_DIR)/arch/arm/src/amebasmart/amebasmart_reboot_reason.c \
	$(OS_DIR)/arch/arm/src/amebasmart/amebasmart_reboot_reason.h \
	$(RTL_DIR)/mbed/targets/hal/rtl8730e/wdt_api.c \
	$(RTL_DIR)/soc/amebad2/fwlib/rom_common/ameba_wdg.c \
	$(RTL_DIR)/soc/amebad2/fwlib/include/ameba_wdg.h \
	$(RTL_DIR)/soc/amebad2/fwlib/include/sysreg_aon.h
WDOG_TESTS = $(addprefix $(OUT_DIR)/,watchdog-smp watchdog-up watchdog-no-timed-wakeup watchdog-no-tick-suppress)
TIMER_WDOG_TESTS = $(addprefix $(OUT_DIR)/,timer-watchdog-smp timer-watchdog-up timer-watchdog-no-reason)
EXAMPLE_TESTS = $(addprefix $(OUT_DIR)/,example-smp example-up)
EXAMPLE_DEPS = $(DRIVER_DEPS) $(TEST_DIR)/example_test.c \
	$(OS_DIR)/../apps/examples/health_monitor/health_monitor_main.c

.PHONY: test
test: $(OUT_DIR)/registry-smp $(OUT_DIR)/registry-up $(OUT_DIR)/driver-smp $(OUT_DIR)/driver-up \
	$(OUT_DIR)/timer-smp $(OUT_DIR)/timer-up $(OUT_DIR)/timer-no-reason $(PM_TESTS) \
	$(TIMER_WDOG_TESTS) $(WDOG_TESTS) $(EXAMPLE_TESTS) $(OUT_DIR)/watchdog-config.ok
	$(OUT_DIR)/registry-smp
	$(OUT_DIR)/registry-up
	$(OUT_DIR)/driver-smp
	$(OUT_DIR)/driver-up
	$(OUT_DIR)/timer-smp
	$(OUT_DIR)/timer-up
	$(OUT_DIR)/timer-no-reason
	$(OUT_DIR)/pm-smp
	$(OUT_DIR)/pm-up
	$(OUT_DIR)/pm-no-timed-wakeup
	$(OUT_DIR)/pm-no-tick-suppress
	$(OUT_DIR)/pm-off
	$(OUT_DIR)/timer-watchdog-smp
	$(OUT_DIR)/timer-watchdog-up
	$(OUT_DIR)/timer-watchdog-no-reason
	$(OUT_DIR)/watchdog-smp
	$(OUT_DIR)/watchdog-up
	$(OUT_DIR)/watchdog-no-timed-wakeup
	$(OUT_DIR)/watchdog-no-tick-suppress
	$(OUT_DIR)/example-smp
	$(OUT_DIR)/example-up

$(OUT_DIR)/example-up: EXAMPLE_FLAGS = -DHEALTH_MONITOR_TEST_UP
$(EXAMPLE_TESTS): $(EXAMPLE_DEPS)
	mkdir -p $(OUT_DIR)
	$(HOSTCC) $(TEST_FLAGS) $(EXAMPLE_FLAGS) $(DRIVER_INCLUDES) $(TEST_DIR)/example_test.c -o $@

$(OUT_DIR)/registry-smp: $(TEST_DEPS)
	mkdir -p $(OUT_DIR)
	$(HOSTCC) $(TEST_FLAGS) $(TEST_INCLUDES) $(TEST_DIR)/registry_test.c -o $@

$(OUT_DIR)/registry-up: $(TEST_DEPS)
	mkdir -p $(OUT_DIR)
	$(HOSTCC) $(TEST_FLAGS) -DHEALTH_MONITOR_TEST_UP $(TEST_INCLUDES) $(TEST_DIR)/registry_test.c -o $@

$(OUT_DIR)/driver-smp: $(DRIVER_DEPS)
	mkdir -p $(OUT_DIR)
	$(HOSTCC) $(TEST_FLAGS) $(DRIVER_INCLUDES) $(TEST_DIR)/driver_test.c -o $@

$(OUT_DIR)/driver-up: $(DRIVER_DEPS)
	mkdir -p $(OUT_DIR)
	$(HOSTCC) $(TEST_FLAGS) -DHEALTH_MONITOR_TEST_UP $(DRIVER_INCLUDES) $(TEST_DIR)/driver_test.c -o $@

$(OUT_DIR)/timer-smp: $(TIMER_DEPS)
	mkdir -p $(OUT_DIR)
	$(HOSTCC) $(TEST_FLAGS) $(TIMER_INCLUDES) $(TEST_DIR)/timer_test.c -o $@

$(OUT_DIR)/timer-up: $(TIMER_DEPS)
	mkdir -p $(OUT_DIR)
	$(HOSTCC) $(TEST_FLAGS) -DHEALTH_MONITOR_TEST_UP $(TIMER_INCLUDES) $(TEST_DIR)/timer_test.c -o $@

$(OUT_DIR)/timer-no-reason: $(TIMER_DEPS)
	mkdir -p $(OUT_DIR)
	$(HOSTCC) $(TEST_FLAGS) -DHEALTH_MONITOR_TEST_NO_REASON $(TIMER_INCLUDES) $(TEST_DIR)/timer_test.c -o $@

$(OUT_DIR)/pm-up: PM_FLAGS = -DHEALTH_MONITOR_TEST_UP
$(OUT_DIR)/pm-no-timed-wakeup: PM_FLAGS = -DHEALTH_MONITOR_TEST_NO_TIMEDWAKEUP
$(OUT_DIR)/pm-no-tick-suppress: PM_FLAGS = -DHEALTH_MONITOR_TEST_NO_TICKSUPPRESS
$(OUT_DIR)/pm-off: PM_FLAGS = -DHEALTH_MONITOR_TEST_OFF

$(PM_TESTS): $(PM_DEPS)
	mkdir -p $(OUT_DIR)
	$(HOSTCC) $(TEST_FLAGS) $(PM_FLAGS) $(PM_INCLUDES) $(TEST_DIR)/pm_test.c -o $@

$(OUT_DIR)/timer-watchdog-up: TIMER_WDOG_FLAGS = -DHEALTH_MONITOR_TEST_UP
$(OUT_DIR)/timer-watchdog-no-reason: TIMER_WDOG_FLAGS = -DHEALTH_MONITOR_TEST_NO_REASON
$(TIMER_WDOG_TESTS): $(TIMER_DEPS)
	mkdir -p $(OUT_DIR)
	$(HOSTCC) $(TEST_FLAGS) -DHEALTH_MONITOR_TEST_WDOG $(TIMER_WDOG_FLAGS) $(TIMER_INCLUDES) $(TEST_DIR)/timer_test.c -o $@

$(OUT_DIR)/watchdog-up: WDOG_FLAGS = -DHEALTH_MONITOR_TEST_UP
$(OUT_DIR)/watchdog-no-timed-wakeup: WDOG_FLAGS = -DHEALTH_MONITOR_TEST_NO_TIMEDWAKEUP
$(OUT_DIR)/watchdog-no-tick-suppress: WDOG_FLAGS = -DHEALTH_MONITOR_TEST_NO_TICKSUPPRESS
$(WDOG_TESTS): $(WDOG_DEPS)
	mkdir -p $(OUT_DIR)
	$(HOSTCC) $(TEST_FLAGS) $(WDOG_FLAGS) $(WDOG_INCLUDES) $(TEST_DIR)/watchdog_test.c -o $@

$(OUT_DIR)/watchdog-config.ok: $(WDOG_DEPS)
	mkdir -p $(OUT_DIR)
	@set -e; for interval in 1000 65000; do \
		$(HOSTCC) $(TEST_FLAGS) $(WDOG_INCLUDES) -fsyntax-only \
			-DCONFIG_WATCHDOG_FOR_IRQ_INTERVAL=$$interval $(TEST_DIR)/watchdog_test.c; \
	done; \
	for interval in 0 999 1001 1500 65001 65536; do \
		if $(HOSTCC) $(TEST_FLAGS) $(WDOG_INCLUDES) -fsyntax-only \
			-DCONFIG_WATCHDOG_FOR_IRQ_INTERVAL=$$interval $(TEST_DIR)/watchdog_test.c \
			>$(OUT_DIR)/watchdog-invalid-$$interval.log 2>&1; then \
			echo "Invalid IRQ watchdog interval accepted: $$interval"; exit 1; \
		fi; \
		grep -q 'RTL8730E IRQ watchdog interval must be' $(OUT_DIR)/watchdog-invalid-$$interval.log; \
	done
	@if $(HOSTCC) $(TEST_FLAGS) $(WDOG_INCLUDES) -fsyntax-only \
		-DHEALTH_MONITOR_TEST_NO_WDOG_WAKEUP $(TEST_DIR)/watchdog_test.c \
		>$(OUT_DIR)/watchdog-missing-pm-hook.log 2>&1; then \
		echo "IRQ watchdog PM without wakeup hook accepted"; exit 1; \
	fi
	@grep -q 'RTL8730E IRQ watchdog PM requires CONFIG_ARCH_HAVE_WDOG_WAKEUP' $(OUT_DIR)/watchdog-missing-pm-hook.log
	touch $@
