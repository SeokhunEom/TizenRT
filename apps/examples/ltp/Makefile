###########################################################################
#
# Copyright 2024 Samsung Electronics All Rights Reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
# express or implied. See the License for the specific language
# governing permissions and limitations under the License.
#
###########################################################################
############################################################################
# apps/examples/ltp/Makefile
#
# Port of NuttX LTP integration to TizenRT.
# Extracts the bundled LTP v20230516 archive, applies TizenRT-specific patches, discovers
# test files with main(), renames main() to unique entry points via
# -Dmain=xxx_main, and registers each as a TASH command.
#
# Build flow:
#   1. make context  - extracts LTP source, applies patches, generates
#                      TASH command registry (.mdat/.pdat files)
#   2. make          - compiles test files with main() renaming
#
# The source archive is a local build input, so Docker builds do not require
# network access. The extraction target is touched after patching because the
# zip tool preserves the archive's old directory timestamp.
#
############################################################################

-include $(TOPDIR)/.config
-include $(TOPDIR)/Make.defs
include $(APPDIR)/Make.defs

# LTP version and download URL

LTP_VERSION     = 20230516
LTP_DOWNLOAD_URL = https://github.com/linux-test-project/ltp/archive/refs/tags/
LTP_UNPACK       = ltp
TESTDIR          = $(LTP_UNPACK)/testcases/open_posix_testsuite

# Test directories to include in the build.
# Batch 1: functional/threads (4 tests) + sched_* query tests
LTP_TEST_SUBDIR  = $(TESTDIR)/functional/threads
LTP_TEST_SUBDIR += $(TESTDIR)/conformance/interfaces/sched_get_priority_max
LTP_TEST_SUBDIR += $(TESTDIR)/conformance/interfaces/sched_get_priority_min
LTP_TEST_SUBDIR += $(TESTDIR)/conformance/interfaces/sched_getscheduler
LTP_TEST_SUBDIR += $(TESTDIR)/conformance/interfaces/sched_yield
LTP_TEST_SUBDIR += $(TESTDIR)/conformance/interfaces/sched_getparam
LTP_TEST_SUBDIR += $(TESTDIR)/conformance/interfaces/sched_rr_get_interval
LTP_TEST_SUBDIR += $(TESTDIR)/conformance/interfaces/sched_setparam
LTP_TEST_SUBDIR += $(TESTDIR)/conformance/interfaces/sched_setscheduler
LTP_TEST_SUBDIR += $(TESTDIR)/conformance/interfaces/pthread_attr_setschedparam
LTP_TEST_SUBDIR += $(TESTDIR)/conformance/interfaces/pthread_attr_setschedpolicy
LTP_TEST_SUBDIR += $(TESTDIR)/conformance/interfaces/pthread_getschedparam
LTP_TEST_SUBDIR += $(TESTDIR)/conformance/interfaces/pthread_setschedparam
LTP_TEST_SUBDIR += $(TESTDIR)/conformance/interfaces/pthread_setschedprio

# Blacklist: patterns that cause build failures on TizenRT.
# Files matching these patterns (via grep) are excluded from the build.

BLACKWORDS += "pthread_mutexattr_setprioceiling"
BLACKWORDS += "pthread_mutexattr_getprioceiling"
BLACKWORDS += "pthread_getattr_np"
BLACKWORDS += "pthread_mutex_getprioceiling"
BLACKWORDS += "CHILD_MAX"
BLACKWORDS += "setpgid("
BLACKWORDS += "PTHREAD_SCOPE_PROCESS"
BLACKWORDS += "setpgrp"
BLACKWORDS += "threads_scenarii.c"
BLACKWORDS += "ucontext.h"
BLACKWORDS += "msync"
BLACKWORDS += "lfind"

# Sporadic server tests - TizenRT does not support _POSIX_SPORADIC_SERVER
BLACKWORDS += "_POSIX_SPORADIC_SERVER"
BLACKWORDS += "sched_setscheduler/19-"
BLACKWORDS += "sched_setparam/25-"

# Shared memory tests - TizenRT does not have sys/shm.h
BLACKWORDS += "sys/shm.h"
BLACKWORDS += "shmget"
BLACKWORDS += "shmat"

# TizenRT does not support fork() - only vfork() is available
BLACKWORDS += "fork("

# TizenRT does not support user identity functions (single-user RTOS)
BLACKWORDS += "getuid"
BLACKWORDS += "setuid"
BLACKWORDS += "geteuid"
BLACKWORDS += "seteuid"
BLACKWORDS += "setgid"

# TizenRT does not have password database
BLACKWORDS += "setpwent"
BLACKWORDS += "getpwent"
BLACKWORDS += "endpwent"

# TizenRT does not support SCHED_OTHER policy (only SCHED_FIFO and SCHED_RR).
# Blacklist specific test files that require SCHED_OTHER support.
# (Cannot use "SCHED_OTHER" as a content grep pattern because it would
#  also exclude passing tests and error-handling tests that merely
#  reference SCHED_OTHER as an invalid policy.)
BLACKSRCS += $(TESTDIR)/conformance/interfaces/pthread_attr_setschedpolicy/1-1.c
BLACKSRCS += $(TESTDIR)/conformance/interfaces/pthread_attr_setschedpolicy/5-1.c
BLACKSRCS += $(TESTDIR)/conformance/interfaces/sched_get_priority_max/1-4.c
BLACKSRCS += $(TESTDIR)/conformance/interfaces/sched_get_priority_min/1-4.c

# pthread_attr_setschedpolicy/2-1.c requires set_affinity_single() which
# is only implemented for SMP builds (see patch 0015). Blacklist on non-SMP.
ifeq ($(CONFIG_SMP),)
BLACKSRCS += $(TESTDIR)/conformance/interfaces/pthread_attr_setschedpolicy/2-1.c
endif



# SCHED_RR tests - blacklist if RR_INTERVAL is not configured
ifneq ($(shell expr $(CONFIG_RR_INTERVAL) \> 0),1)
  BLACKWORDS += "SCHED_RR"
endif


# TizenRT uses CONFIG_DISABLE_PTHREAD=n to enable pthread (inverse convention).
# If pthread is disabled, blacklist all pthread tests.
ifeq ($(CONFIG_DISABLE_PTHREAD),y)
  BLACKWORDS += "pthread"
endif

# Note: Signal, mqueue, and timer tests are not included in the initial
# port (only functional/threads tests are). When expanding to more test
# categories, add conditional blacklist entries here based on TizenRT's
# CONFIG_DISABLE_* options.


# Pattern to find files containing main()
MAINWORDS += "main("

# --- Test discovery and filtering (at parse time) ---
# This logic mirrors NuttX's approach. Only runs if LTP source exists.

ifneq ($(wildcard $(TESTDIR)),)

# Step 1: Find all .c files in the test subdirectory
LTP_ORIGS := $(shell find $(LTP_TEST_SUBDIR) -name "*.c")

# Step 2: Build blacklist by grepping for blacklisted words
ifneq ($(LTP_ORIGS),)
$(foreach word, $(BLACKWORDS), $(eval BLACKLIST += $(shell grep -lr $(word) $(LTP_ORIGS))))
endif

# Also remove explicitly blacklisted source files (BLACKSRCS)
BLACKLIST += $(BLACKSRCS)

# Remove blacklisted files from the list
LTP_ORIGS := $(filter-out $(BLACKLIST), $(LTP_ORIGS))

# Step 3: Find files containing main()
ifneq ($(LTP_ORIGS),)
$(foreach word, $(MAINWORDS), $(eval LTP_MAINCSRCS += $(shell grep -lr $(word) $(LTP_ORIGS))))
endif

# Non-main source files (compiled into libapps as support code)
LTP_CSRCS := $(filter-out $(LTP_MAINCSRCS), $(LTP_ORIGS))

endif # wildcard $(TESTDIR)

# Build configuration

STACKSIZE     = $(CONFIG_EXAMPLES_LTP_STACKSIZE)
PRIORITY      = $(CONFIG_EXAMPLES_LTP_PRIORITY)
THREADEXEC    = TASH_EXECMD_ASYNC

# Source files
ASRCS =
CSRCS = ltp_runner.c $(LTP_CSRCS)
MAINSRC = $(LTP_MAINCSRCS)

AOBJS = $(ASRCS:.S=$(OBJEXT))
COBJS = $(CSRCS:.c=$(OBJEXT))
MAINOBJ = $(MAINSRC:.c=$(OBJEXT))

SRCS = $(ASRCS) $(CSRCS) $(MAINSRC)
OBJS = $(AOBJS) $(COBJS)

ifneq ($(CONFIG_BUILD_KERNEL),y)
  OBJS += $(MAINOBJ)
endif

ifeq ($(CONFIG_WINDOWS_NATIVE),y)
  BIN = $(APPDIR)\libapps$(LIBEXT)
else
ifeq ($(WINTOOL),y)
  BIN = $(APPDIR)\\libapps$(LIBEXT)
else
  BIN = $(APPDIR)/libapps$(LIBEXT)
endif
endif

ROOTDEPPATH = --dep-path . --dep-path $(LTP_UNPACK)

VPATH = $(LTP_UNPACK)

# Include paths for LTP headers
CFLAGS += -I$(CURDIR)
CFLAGS += -I$(TESTDIR)/include
CFLAGS += -I$(TESTDIR)/../../include

# Force-include config.h for TizenRT-specific feature definitions
# (e.g. SIGTERM definition, feature detection macros)
CFLAGS += -include $(CURDIR)/config.h

# Define __TIZENRT__ for conditional compilation in LTP tests
CFLAGS += -D__TIZENRT__
ifeq ($(CONFIG_SMP),y)
CFLAGS += -DCONFIG_SMP
endif
# Relax warning checks - LTP tests generate many expected warnings
CFLAGS += -Wno-strict-prototypes -Wno-return-type -Wno-format -Wno-uninitialized
CFLAGS += -Wno-unused-variable -Wno-unused-function -Wno-unused-but-set-variable -Wno-unused-value
CFLAGS += -Wno-int-conversion -Wno-shadow
CFLAGS += -Wno-incompatible-pointer-types -Wno-overflow -Wno-int-to-pointer-cast
CFLAGS += -Wno-shift-count-overflow

all: .built
.PHONY: clean depend distclean context

# --- main() renaming mechanism ---
# For each test source file, compile with -Dmain=<funcname> to rename
# the main() function to a unique entry point.
# This is the same approach used by NuttX (apps/Application.mk lines 352-356).
#
# The function name is derived from the file path:
#   ltp/.../condvar/pthread_cond_wait_1.c
#   -> ltp_condvar_pthread_cond_wait_1_main
#
# We use a shell command in the recipe to compute the function name,
# since Make functions cannot easily process $< at recipe execution time.

$(MAINOBJ): %$(OBJEXT): %.c
	$(Q) dir=$$(basename $$(dirname $<)); \
	file=$$(basename $< .c); \
	funcname=ltp_$$(echo $$dir | sed 's/-/_/g')_$$(echo $$file | sed 's/-/_/g')_main; \
	echo "CC: $< (main=$$funcname)"; \
	$(CC) -c $(CFLAGS) -Dmain=$$funcname $< -o $@

# Compile support (non-main) source files normally
$(COBJS): %$(OBJEXT): %.c
	$(call COMPILE, $<, $@)

$(AOBJS): %$(OBJEXT): %.S
	$(call ASSEMBLE, $<, $@)

.built: $(OBJS) $(MAINOBJ)
	$(call ARCHIVE, $(BIN), $(OBJS) $(MAINOBJ))
	@touch .built

# --- LTP source extraction and patching ---
# Note: The LTP zip file (ltp-20230516.zip) is pre-downloaded and placed
# in this directory. The download step is commented out to avoid network
# issues in Docker build environments.

# ltp-$(LTP_VERSION).zip:
# 	$(Q) echo "Downloading LTP v$(LTP_VERSION)..."
# 	$(Q) wget -q -O ltp-$(LTP_VERSION).zip $(LTP_DOWNLOAD_URL)$(LTP_VERSION).zip

$(LTP_UNPACK): ltp-$(LTP_VERSION).zip
	$(Q) echo "Extracting LTP source..."
	$(Q) rm -rf $(LTP_UNPACK)
	$(Q) unzip -q -oX ltp-$(LTP_VERSION).zip
	$(Q) mv ltp-$(LTP_VERSION) $(LTP_UNPACK)
#	$(Q) chmod -R u+w $(LTP_UNPACK)
#	$(Q) chown -R $(shell id -u):$(shell id -g) $(LTP_UNPACK) 2>/dev/null || true
	$(Q) echo "Applying TizenRT-specific patches..."
	$(Q) set -e; for p in patches/*.patch; do patch -d $(LTP_UNPACK) -p1 < $$p; done
	$(Q) touch $(LTP_UNPACK)

# --- TASH command registration ---
# Register each LTP test as a TASH command using the builtin registry.
# The REGISTER macro (from apps/Make.defs) creates .mdat and .pdat files
# in the builtin registry, which are compiled into builtin_list.c.
#
# We use a shell script (ltp_register.sh) to discover test files and
# generate the registry entries. This runs during the context phase,
# after the LTP source has been downloaded and patched.

ifeq ($(CONFIG_BUILTIN_APPS)$(CONFIG_EXAMPLES_LTP),yy)
context: $(LTP_UNPACK)
	$(Q) echo "Registering LTP TASH commands..."
	$(Q) CURDIR="$(CURDIR)" \
		TESTDIR="$(CURDIR)/$(TESTDIR)" \
		LTP_TEST_SUBDIR="$(CURDIR)/$(LTP_TEST_SUBDIR)" \
		BUILTIN_REGISTRY="$(BUILTIN_REGISTRY)" \
		STACKSIZE="$(STACKSIZE)" \
		PRIORITY="$(PRIORITY)" \
		THREADEXEC="$(THREADEXEC)" \
		CONFIG_DISABLE_PTHREAD="$(CONFIG_DISABLE_PTHREAD)" \
		CONFIG_SMP="$(CONFIG_SMP)" \
		sh $(CURDIR)/ltp_register.sh
else
context:
endif

install:

.depend: Makefile ltp_registry.inc $(SRCS)
	@$(MKDEP) $(ROOTDEPPATH) "$(CC)" -- $(CFLAGS) -- $(SRCS) >Make.dep
	@touch $@

depend: .depend

clean:
	$(call DELFILE, .built)
	$(call CLEAN)

distclean: clean
	$(call DELFILE, Make.dep)
	$(call DELFILE, .depend)
	$(Q) rm -rf $(LTP_UNPACK)

-include Make.dep
.PHONY: preconfig
preconfig:
