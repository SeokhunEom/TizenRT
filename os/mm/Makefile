###########################################################################
#
# Copyright 2016 Samsung Electronics All Rights Reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
# either express or implied. See the License for the specific
# language governing permissions and limitations under the License.
#
###########################################################################
############################################################################
# mm/Makefile
#
#   Copyright (C) 2007, 2012, 2013-2014 Gregory Nutt. All rights reserved.
#   Author: Gregory Nutt <gnutt@nuttx.org>
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in
#    the documentation and/or other materials provided with the
#    distribution.
# 3. Neither the name NuttX nor the names of its contributors may be
#    used to endorse or promote products derived from this software
#    without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
# FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
# COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
# INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
# BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
# OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
# AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
# ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.
#
############################################################################

-include $(TOPDIR)/Make.defs

# REVISIT: Backslash causes problems in $(COBJS) target
DELIM := $(strip /)

# CFLAGS

ifeq ($(CONFIG_BUILD_PROTECTED),y)
ifeq ($(CONFIG_WINDOWS_NATIVE),y)
  KDEFINE = ${shell $(TOPDIR)\tools\define.bat "$(CC)" __KERNEL__}
else
  KDEFINE = ${shell $(TOPDIR)/tools/define.sh "$(CC)" __KERNEL__}
endif
endif

# include kernel header files to use kernel api during memory allocation failure
ifeq ($(CONFIG_APP_BINARY_SEPARATION),y)
CFLAGS += ${shell $(INCDIR) $(INCDIROPT) "$(CC)" "$(TOPDIR)$(DELIM)kernel"}
endif

# Sources and paths

ASRCS  =
CSRCS  =

DEPPATH = --dep-path .
VPATH = .

# Keep the native Make build. The upstream Make.defs are tracked originals;
# only mm_heap is enabled. TizenRT owns heap routing and public libc wrappers.
UPSTREAM_SRCS = mm_initialize.c mm_lock.c mm_malloc_size.c mm_shrinkchunk.c
UPSTREAM_SRCS += mm_brkaddr.c mm_calloc.c mm_extend.c mm_free.c mm_mallinfo.c
UPSTREAM_SRCS += mm_malloc.c mm_foreach.c mm_memalign.c mm_realloc.c mm_zalloc.c
UPSTREAM_SRCS += mm_heapmember.c
ifeq ($(CONFIG_DEBUG_MM),y)
UPSTREAM_SRCS += mm_checkcorruption.c
endif
CSRCS += $(UPSTREAM_SRCS) nuttx_heap_bridge.c nuttx_os.c
CSRCS += mm_api.c mm_debug.c mm_getheap.c mm_sem.c mm_manage_allocfail.c
CSRCS += mm_heap_regioninfo.c
ifeq ($(CONFIG_DEBUG_MM_HEAPINFO),y)
CSRCS += mm_heapinfo_utils.c
ifeq ($(CONFIG_HEAPINFO_USER_GROUP),y)
CSRCS += mm_heapinfo_group.c
endif
endif
DEPPATH += --dep-path mm_heap --dep-path tizenrt --dep-path tizenrt/bridge --dep-path compat
VPATH += :mm_heap:tizenrt:tizenrt/bridge:compat
include tizenrt/umm_heap/Make.defs
include tizenrt/kmm_heap/Make.defs

MMDIR := $(TOPDIR)/mm
NUTTX_PRIVATE_FLAGS = -I$(MMDIR)/compat/include -I$(MMDIR) -include $(MMDIR)/compat/symbol_map.h

BINDIR ?= bin

AOBJS = $(patsubst %.S, $(BINDIR)$(DELIM)%$(OBJEXT), $(ASRCS))
COBJS = $(patsubst %.c, $(BINDIR)$(DELIM)%$(OBJEXT), $(CSRCS))

SRCS = $(ASRCS) $(CSRCS)
OBJS = $(AOBJS) $(COBJS)

UBIN = libumm$(LIBEXT)
KBIN = libkmm$(LIBEXT)
BIN ?= libmm$(LIBEXT)

all: $(BIN)
.PHONY: clean distclean heap_regioninfo

$(AOBJS): $(BINDIR)$(DELIM)%$(OBJEXT): %.S
	$(call ASSEMBLE, $<, $@)
	$(call MOVEOBJ,$(patsubst $(BINDIR)$(DELIM)%$(OBJEXT),%,$@),$(BINDIR))

# Private NuttX names and header projections never leak into TizenRT callers.
$(addprefix $(BINDIR)/,$(UPSTREAM_SRCS:.c=$(OBJEXT)) nuttx_heap_bridge$(OBJEXT)): CFLAGS += $(NUTTX_PRIVATE_FLAGS)
$(BINDIR)/nuttx_os$(OBJEXT): CFLAGS += -I$(MMDIR)/compat/include -I$(TOPDIR)/kernel

$(COBJS): $(BINDIR)$(DELIM)%$(OBJEXT): %.c
	$(call COMPILE, $<, $@)
	$(call MOVEOBJ,$(patsubst $(BINDIR)$(DELIM)%$(OBJEXT),%,$@),$(BINDIR))

# Memory manager for the flat build

$(BIN):	$(OBJS)
	$(call ARCHIVE, $@, $(OBJS))

# Memory manager for the user phase of the two-pass kernel build

ifneq ($(BIN),$(UBIN))
$(UBIN):
	$(Q) $(MAKE) $(UBIN) BIN=$(UBIN) BINDIR=ubin TOPDIR=$(TOPDIR) EXTRADEFINES=$(EXTRADEFINES)
endif

# Memory manager for the kernel phase of the two-pass kernel build

ifneq ($(BIN),$(KBIN))
$(KBIN):
	$(Q) $(MAKE) $(KBIN) BIN=$(KBIN) BINDIR=kbin TOPDIR=$(TOPDIR) EXTRADEFINES=$(EXTRADEFINES)
endif

heap_regioninfo:
	$(Q) $(TOPDIR)/tools/mk_heap_regioninfo.sh

context: heap_regioninfo
ifeq ($(CONFIG_HEAPINFO_USER_GROUP),y)
GROUP_DELIM_NUM = "${shell echo $(CONFIG_HEAPINFO_USER_GROUP_LIST) | grep -o '/' | wc -l}"
THREAD_DELIM_NUM = "${shell echo $(CONFIG_HEAPINFO_USER_GROUP_LIST) | grep -o ',' | wc -l}"

${shell printf '%s\n%s\n%s\n%s\n' "#define HEAPINFO_USER_GROUP_DELIM_NUM $(GROUP_DELIM_NUM)" "#define HEAPINFO_THREAD_DELIM_NUM $(THREAD_DELIM_NUM)" "#define HEAPINFO_USER_GROUP_NUM (HEAPINFO_USER_GROUP_DELIM_NUM + 1)" "#define HEAPINFO_THREAD_NUM (HEAPINFO_USER_GROUP_NUM + HEAPINFO_THREAD_DELIM_NUM)"> $(TOPDIR)$(DELIM)include$(DELIM)tinyara$(DELIM)mm$(DELIM)heapinfo_internal.h}
endif

# Dependencies

.depend: Makefile $(SRCS)
	$(Q) $(MAKE) dependency-file BIN=$(UBIN) BINDIR=ubin DEPFILE=Make_ubin.dep
	$(Q) $(MAKE) dependency-file BIN=$(KBIN) BINDIR=kbin DEPFILE=Make_kbin.dep EXTRADEFINES="$(EXTRADEFINES) $(KDEFINE)"
	$(Q) $(MAKE) dependency-file BINDIR=bin DEPFILE=Make_bin.dep
	$(Q) touch $@

.PHONY: dependency-file
dependency-file:
	$(Q) $(MKDEP) --obj-path $(BINDIR) --obj-suffix $(OBJEXT) $(DEPPATH) "$(CC)" -- $(CFLAGS) $(NUTTX_PRIVATE_FLAGS) -- $(UPSTREAM_SRCS) nuttx_heap_bridge.c >$(DEPFILE)
	$(Q) $(MKDEP) --obj-path $(BINDIR) --obj-suffix $(OBJEXT) $(DEPPATH) "$(CC)" -- $(CFLAGS) -I$(MMDIR)/compat/include -I$(TOPDIR)/kernel -- nuttx_os.c >>$(DEPFILE)
	$(Q) $(MKDEP) --obj-path $(BINDIR) --obj-suffix $(OBJEXT) $(DEPPATH) "$(CC)" -- $(CFLAGS) -- $(filter-out $(UPSTREAM_SRCS) nuttx_heap_bridge.c nuttx_os.c,$(CSRCS)) >>$(DEPFILE)

depend: .depend

# Clean most derived files, retaining the configuration

clean:
	$(Q) $(MAKE) -C bin  clean TOPDIR=$(TOPDIR)
	$(Q) $(MAKE) -C ubin clean TOPDIR=$(TOPDIR)
	$(Q) $(MAKE) -C kbin clean TOPDIR=$(TOPDIR)
	$(call DELFILE, $(BIN))
	$(call DELFILE, $(UBIN))
	$(call DELFILE, $(KBIN))
ifeq ($(CONFIG_HEAPINFO_USER_GROUP),y)
	$(call DELFILE, $(TOPDIR)$(DELIM)include$(DELIM)tinyara$(DELIM)mm$(DELIM)heapinfo_internal.h)
endif
	$(call CLEAN)

# Deep clean -- removes all traces of the configuration

distclean: clean
	$(Q) $(MAKE) -C bin  distclean TOPDIR=$(TOPDIR)
	$(Q) $(MAKE) -C ubin distclean TOPDIR=$(TOPDIR)
	$(Q) $(MAKE) -C kbin distclean TOPDIR=$(TOPDIR)
	$(call DELFILE, $(TOPDIR)$(DELIM)mm$(DELIM)tizenrt$(DELIM)mm_heap_regioninfo.c)
	$(call DELFILE, Make_bin.dep)
	$(call DELFILE, Make_ubin.dep)
	$(call DELFILE, Make_kbin.dep)
	$(call DELFILE, .depend)

-include Make_bin.dep
-include Make_ubin.dep
-include Make_kbin.dep
