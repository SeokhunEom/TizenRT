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
	$(TEST_DIR)/include/sched/sched.h $(TEST_DIR)/../health_monitor.c \
	$(TEST_DIR)/../health_monitor.h $(OS_DIR)/include/tinyara/health_monitor.h

.PHONY: test
test: $(OUT_DIR)/registry-smp $(OUT_DIR)/registry-up
	$(OUT_DIR)/registry-smp
	$(OUT_DIR)/registry-up

$(OUT_DIR)/registry-smp: $(TEST_DEPS)
	mkdir -p $(OUT_DIR)
	$(HOSTCC) $(TEST_FLAGS) $(TEST_INCLUDES) $(TEST_DIR)/registry_test.c -o $@

$(OUT_DIR)/registry-up: $(TEST_DEPS)
	mkdir -p $(OUT_DIR)
	$(HOSTCC) $(TEST_FLAGS) -DHEALTH_MONITOR_TEST_UP $(TEST_INCLUDES) $(TEST_DIR)/registry_test.c -o $@
