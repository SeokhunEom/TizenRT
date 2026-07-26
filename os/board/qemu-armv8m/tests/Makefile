REPO_ROOT := $(abspath $(CURDIR)/../../../..)
OS_ROOT := $(REPO_ROOT)/os
BUILD_DIR := $(CURDIR)/.build
CC ?= cc

WARNINGS := -std=gnu99 -Wall -Wextra -Werror -Wconversion -Wshadow
ABI_INCLUDES := -I$(CURDIR)/include -idirafter $(OS_ROOT)/include
EXC_RETURN_INCLUDES := -I$(CURDIR)/include \
	-I$(OS_ROOT)/arch/arm/src/armv8-m
EXEC_INCLUDES := -I$(CURDIR)/exec_include -I$(OS_ROOT)/kernel \
	-idirafter $(OS_ROOT)/include
APP_FLAGS := -DCONFIG_APP_BINARY_SEPARATION=1 -DCONFIG_NUM_APPS=1 \
	-DCONFIG_APP1_BIN_NAME=\"app1\" -DCONFIG_OPTIMIZE_APP_RELOAD_TIME=1
EXEC_FLAGS := $(APP_FLAGS) -DCONFIG_BINFMT_ENABLE=1 -DCONFIG_DEBUG=1 \
	-DCONFIG_DISABLE_SIGNALS=1 -DCONFIG_TASK_NAME_SIZE=31 -D__KERNEL__=1
EXEC_HOST_ABI_FLAGS := -arch x86_64 -Wno-int-to-pointer-cast \
	-Wno-pointer-to-int-cast
EXEC_HOST_LINK_FLAGS := -Wl,-pagezero_size,0x1000
LOADER_INCLUDES := -I$(CURDIR)/loader_include -idirafter $(OS_ROOT)/include
LOADER_FLAGS := -DCONFIG_APP_BINARY_SEPARATION=1 -DCONFIG_NUM_APPS=1 \
	-DCONFIG_APP1_BIN_NAME=\"app1\" -DCONFIG_XIP_ELF=1 \
	-DCONFIG_SUPPORT_COMMON_BINARY=1 \
	-DCONFIG_COMMON_BINARY_NAME=\"common\" -DCONFIG_BOARD_INITIALIZE=1

.PHONY: all check clean abi exc-return-matrix exec-no-bm \
	binary-manager-compat loader loader-compile-matrix

all: check

check: clean abi exc-return-matrix exec-no-bm loader-compile-matrix loader
	@$(BUILD_DIR)/binary-header-hidden
	@$(BUILD_DIR)/abi-no-bm-elf
	@$(BUILD_DIR)/abi-no-bm-xip
	@$(BUILD_DIR)/abi-bm-elf
	@$(BUILD_DIR)/abi-bm-xip
	@$(BUILD_DIR)/binfmt-no-bm-elf
	@$(BUILD_DIR)/binfmt-no-bm-xip
	@$(BUILD_DIR)/binfmt-bm-elf
	@$(BUILD_DIR)/binfmt-bm-xip
	@$(BUILD_DIR)/exec-no-bm
	@$(BUILD_DIR)/qemu-loader all

abi: $(BUILD_DIR)/binary-header-hidden \
	$(BUILD_DIR)/abi-no-bm-elf $(BUILD_DIR)/abi-no-bm-xip \
	$(BUILD_DIR)/abi-bm-elf $(BUILD_DIR)/abi-bm-xip \
	$(BUILD_DIR)/binfmt-no-bm-elf $(BUILD_DIR)/binfmt-no-bm-xip \
	$(BUILD_DIR)/binfmt-bm-elf $(BUILD_DIR)/binfmt-bm-xip

binary-manager-compat: $(BUILD_DIR)/abi-bm-elf $(BUILD_DIR)/abi-bm-xip \
	$(BUILD_DIR)/binfmt-bm-elf $(BUILD_DIR)/binfmt-bm-xip
	@$(BUILD_DIR)/abi-bm-elf
	@$(BUILD_DIR)/abi-bm-xip
	@$(BUILD_DIR)/binfmt-bm-elf
	@$(BUILD_DIR)/binfmt-bm-xip

exec-no-bm: $(BUILD_DIR)/exec-no-bm
	@$(BUILD_DIR)/exec-no-bm

loader: $(BUILD_DIR)/qemu-loader
	@$(BUILD_DIR)/qemu-loader all

loader-compile-matrix:
	$(CC) $(WARNINGS) -Wno-int-to-pointer-cast \
		$(LOADER_INCLUDES) \
		-DCONFIG_APP_BINARY_SEPARATION=1 -DCONFIG_NUM_APPS=1 \
		-DCONFIG_APP1_BIN_NAME=\"app1\" -DCONFIG_BOARD_INITIALIZE=1 \
		-fsyntax-only ../src/qemu_armv8m_boot.c
	$(CC) $(WARNINGS) -Wno-int-to-pointer-cast \
		$(LOADER_INCLUDES) \
		$(LOADER_FLAGS) -fsyntax-only ../src/qemu_armv8m_boot.c

exc-return-matrix:
	$(CC) $(WARNINGS) -fsyntax-only $(EXC_RETURN_INCLUDES) \
		-DCONFIG_ARCH_CHIP_QEMU_ARMV8M=1 -DTEST_EXPECT_QEMU=1 \
		-DTEST_EXPECT_FPU=0 -DTEST_EXPECT_BASE=0xffffffe1UL \
		-DTEST_EXPECT_HANDLER=0xfffffff1UL \
		-DTEST_EXPECT_PRIVTHR=0xfffffff9UL \
		-DTEST_EXPECT_UNPRIVTHR=0xfffffffdUL test_exc_return_values.c
	$(CC) $(WARNINGS) -fsyntax-only $(EXC_RETURN_INCLUDES) \
		-DCONFIG_ARCH_CHIP_QEMU_ARMV8M=1 -DCONFIG_ARCH_FPU=1 \
		-DCONFIG_ARM_CMNVECTOR=1 -DTEST_EXPECT_QEMU=1 \
		-DTEST_EXPECT_FPU=1 -DTEST_EXPECT_BASE=0xffffffe1UL \
		-DTEST_EXPECT_HANDLER=0xfffffff1UL \
		-DTEST_EXPECT_PRIVTHR=0xffffffe9UL \
		-DTEST_EXPECT_UNPRIVTHR=0xffffffedUL test_exc_return_values.c
	$(CC) $(WARNINGS) -fsyntax-only $(EXC_RETURN_INCLUDES) \
		-DTEST_EXPECT_QEMU=0 -DTEST_EXPECT_FPU=0 \
		-DTEST_EXPECT_BASE=0xffffffa0UL \
		-DTEST_EXPECT_HANDLER=0xffffffb0UL \
		-DTEST_EXPECT_PRIVTHR=0xffffffb8UL \
		-DTEST_EXPECT_UNPRIVTHR=0xffffffbcUL test_exc_return_values.c
	$(CC) $(WARNINGS) -fsyntax-only $(EXC_RETURN_INCLUDES) \
		-DCONFIG_ARCH_FPU=1 -DCONFIG_ARM_CMNVECTOR=1 \
		-DTEST_EXPECT_QEMU=0 -DTEST_EXPECT_FPU=1 \
		-DTEST_EXPECT_BASE=0xffffffa0UL \
		-DTEST_EXPECT_HANDLER=0xffffffb0UL \
		-DTEST_EXPECT_PRIVTHR=0xffffffa8UL \
		-DTEST_EXPECT_UNPRIVTHR=0xffffffacUL test_exc_return_values.c

$(BUILD_DIR):
	@mkdir -p $@

$(BUILD_DIR)/abi-no-bm-elf: test_binary_header_abi.c | $(BUILD_DIR)
	$(CC) $(WARNINGS) $(ABI_INCLUDES) $(APP_FLAGS) $< -o $@

$(BUILD_DIR)/binary-header-hidden: test_binary_header_hidden.c | $(BUILD_DIR)
	$(CC) $(WARNINGS) $(ABI_INCLUDES) $< -o $@

$(BUILD_DIR)/abi-no-bm-xip: test_binary_header_abi.c | $(BUILD_DIR)
	$(CC) $(WARNINGS) $(ABI_INCLUDES) $(APP_FLAGS) -DCONFIG_XIP_ELF=1 $< -o $@

$(BUILD_DIR)/abi-bm-elf: test_binary_header_abi.c | $(BUILD_DIR)
	$(CC) $(WARNINGS) $(ABI_INCLUDES) -DCONFIG_BINARY_MANAGER=1 $< -o $@

$(BUILD_DIR)/abi-bm-xip: test_binary_header_abi.c | $(BUILD_DIR)
	$(CC) $(WARNINGS) $(ABI_INCLUDES) -DCONFIG_BINARY_MANAGER=1 \
		-DCONFIG_XIP_ELF=1 $< -o $@

$(BUILD_DIR)/binfmt-no-bm-elf: test_binfmt_header.c | $(BUILD_DIR)
	$(CC) $(WARNINGS) $(ABI_INCLUDES) $(APP_FLAGS) $< -o $@

$(BUILD_DIR)/binfmt-no-bm-xip: test_binfmt_header.c | $(BUILD_DIR)
	$(CC) $(WARNINGS) $(ABI_INCLUDES) $(APP_FLAGS) -DCONFIG_XIP_ELF=1 $< -o $@

$(BUILD_DIR)/binfmt-bm-elf: test_binfmt_header.c | $(BUILD_DIR)
	$(CC) $(WARNINGS) $(ABI_INCLUDES) -DCONFIG_BINARY_MANAGER=1 $< -o $@

$(BUILD_DIR)/binfmt-bm-xip: test_binfmt_header.c | $(BUILD_DIR)
	$(CC) $(WARNINGS) $(ABI_INCLUDES) -DCONFIG_BINARY_MANAGER=1 \
		-DCONFIG_XIP_ELF=1 $< -o $@

$(BUILD_DIR)/exec-no-bm: test_exec_module_no_bm.c \
		$(OS_ROOT)/binfmt/binfmt_execmodule.c | $(BUILD_DIR)
	$(CC) $(WARNINGS) $(EXEC_HOST_ABI_FLAGS) $(EXEC_INCLUDES) $(EXEC_FLAGS) \
		$< $(EXEC_HOST_LINK_FLAGS) -o $@

$(BUILD_DIR)/qemu-loader: test_qemu_armv8m_loader.c \
		../src/qemu_armv8m_boot.c | $(BUILD_DIR)
	$(CC) $(WARNINGS) -Wno-int-to-pointer-cast \
		$(EXEC_HOST_ABI_FLAGS) $(LOADER_INCLUDES) \
		$(LOADER_FLAGS) $< $(EXEC_HOST_LINK_FLAGS) -o $@

clean:
	@rm -f $(BUILD_DIR)/binary-header-hidden \
		$(BUILD_DIR)/abi-no-bm-elf $(BUILD_DIR)/abi-no-bm-xip \
		$(BUILD_DIR)/abi-bm-elf $(BUILD_DIR)/abi-bm-xip \
		$(BUILD_DIR)/binfmt-no-bm-elf $(BUILD_DIR)/binfmt-no-bm-xip \
		$(BUILD_DIR)/binfmt-bm-elf $(BUILD_DIR)/binfmt-bm-xip \
		$(BUILD_DIR)/exec-no-bm $(BUILD_DIR)/old-header-no-bm \
		$(BUILD_DIR)/old-exec-no-bm $(BUILD_DIR)/qemu-loader
	@rmdir $(BUILD_DIR) 2>/dev/null || true
