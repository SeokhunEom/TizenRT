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
# Config.mk
# Global build rules and macros.
#
#   Copyright (C) 2011, 2013-2014 Gregory Nutt. All rights reserved.
#   Author: Richard Cochran
#           Gregory Nutt <gnutt@nuttx.org>
#
# This file (along with $(TOPDIR)/.config) must be included by every
# configuration-specific Make.defs file.
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

# These are configuration variables that are quoted by configuration tool
# but which must be unquoated when used in the build system.

CONFIG_ARCH       := $(patsubst "%",%,$(strip $(CONFIG_ARCH)))
CONFIG_ARCH_CHIP  := $(patsubst "%",%,$(strip $(CONFIG_ARCH_CHIP)))
CONFIG_ARCH_BOARD := $(patsubst "%",%,$(strip $(CONFIG_ARCH_BOARD)))

# Some defaults just to prohibit some bad behavior if for some reason they
# are not defined

OBJEXT ?= .o
LIBEXT ?= .a

# DELIM - Path segment delimiter character
#
# Depends on this settings defined in board-specific defconfig file installed
# at $(TOPDIR)/.config:
#
#   CONFIG_WINDOWS_NATIVE - Defined for a Windows native build

ifeq ($(CONFIG_WINDOWS_NATIVE),y)
  DELIM = $(strip \)
else
  DELIM = $(strip /)
endif

# INCDIR - Convert a list of directory paths to a list of compiler include
#   directirves
# Example: CFFLAGS += ${shell $(INCDIR) [options] "compiler" "dir1" "dir2" "dir2" ...}
#
# Note that the compiler string and each directory path string must quoted if
# they contain spaces or any other characters that might get mangled by the
# shell
#
# Depends on this setting passed as a make commaond line definition from the
# toplevel Makefile:
#
#   TOPDIR - The path to the top level TinyAra directory in the form
#     appropriate for the current build environment
#
# Depends on this settings defined in board-specific defconfig file installed
# at $(TOPDIR)/.config:
#
#   CONFIG_WINDOWS_NATIVE - Defined for a Windows native build

ifeq ($(CONFIG_WINDOWS_NATIVE),y)
  INCDIR = "$(TOPDIR)\tools\incdir.bat"
else
  INCDIR = "$(TOPDIR)/tools/incdir.sh"
endif

# TOOLCHAIN_ARTIFACT_ROOT - Optional root for full toolchain artifacts

TIZENRT_FULL_ARTIFACTS ?=
TOOLCHAIN_ARTIFACT_ROOT ?= $(TOPDIR)/../build/output/toolchain-artifacts

ifneq ($(TIZENRT_FULL_ARTIFACTS),)
define TOOLCHAIN_ARTIFACT_PATHS
	repo_root=`cd "$(TOPDIR)/.." && pwd -P`; \
	artifact_src="$(strip $1)"; \
	artifact_obj="$(strip $2)"; \
	src_dirname=`dirname "$$artifact_src"`; \
	src_name=`basename "$$artifact_src"`; \
	src_abs_dir=`cd "$$src_dirname" && pwd -P`; \
	src_rel=`printf '%s\n' "$$src_abs_dir/$$src_name" | sed -e "s#^$$repo_root/##" -e 's#^/##'`; \
	src_dir=`dirname "$$src_rel"`; \
	obj_base=`basename "$$artifact_obj" $(OBJEXT)`; \
	compile_dir="$(TOOLCHAIN_ARTIFACT_ROOT)/compile/$$src_dir"; \
	assemble_dir="$(TOOLCHAIN_ARTIFACT_ROOT)/assemble/$$src_dir"; \
	archive_dir="$(TOOLCHAIN_ARTIFACT_ROOT)/archive"; \
	link_dir="$(TOOLCHAIN_ARTIFACT_ROOT)/link"; \
	mkdir -p "$$compile_dir" "$$assemble_dir" "$$archive_dir" "$$link_dir"
endef
endif

# PREPROCESS - Default macro to run the C pre-processor
# Example: $(call PREPROCESS, in-file, out-file)
#
# Depends on these settings defined in board-specific Make.defs file
# installed at $(TOPDIR)/Make.defs:
#
#   CPP - The command to invoke the C pre-processor
#   CPPFLAGS - Options to pass to the C pre-processor

define PREPROCESS
	@echo "CPP: $1->$2"
	$(Q) $(CPP) $(CPPFLAGS) $1 -o $2
endef

# COMPILE - Default macro to compile one C file
# Example: $(call COMPILE, in-file, out-file)
#
# Depends on these settings defined in board-specific Make.defs file
# installed at $(TOPDIR)/Make.defs:
#
#   CC - The command to invoke the C compiler
#   CFLAGS - Options to pass to the C compiler

ifneq ($(TIZENRT_FULL_ARTIFACTS),)
define COMPILE
	@echo "CC: $1"
	$(Q) $(call TOOLCHAIN_ARTIFACT_PATHS,$1,$2); \
	$(CC) -E $(CFLAGS) $1 -o "$$compile_dir/$$obj_base.i" && \
	$(CC) -S $(CFLAGS) $1 -o "$$compile_dir/$$obj_base.s" && \
	$(CC) -c $(CFLAGS) -MMD -MP -MF "$$compile_dir/$$obj_base.d" -Wa,-adhln="$$compile_dir/$$obj_base.lst" $1 -o $2 && \
	cp $2 "$$compile_dir/$$obj_base$(OBJEXT)"
endef
else
define COMPILE
	@echo "CC: $1"
	$(Q) $(CC) -c $(CFLAGS) $1 -o $2
endef
endif

# COMPILEXX - Default macro to compile one C++ file
# Example: $(call COMPILEXX, in-file, out-file)
#
# Depends on these settings defined in board-specific Make.defs file
# installed at $(TOPDIR)/Make.defs:
#
#   CXX - The command to invoke the C++ compiler
#   CXXFLAGS - Options to pass to the C++ compiler

ifneq ($(TIZENRT_FULL_ARTIFACTS),)
define COMPILEXX
	@echo "CXX: $1"
	$(Q) $(call TOOLCHAIN_ARTIFACT_PATHS,$1,$2); \
	$(CXX) -E $(CXXFLAGS) $1 -o "$$compile_dir/$$obj_base.ii" && \
	$(CXX) -S $(CXXFLAGS) $1 -o "$$compile_dir/$$obj_base.s" && \
	$(CXX) -c $(CXXFLAGS) -MMD -MP -MF "$$compile_dir/$$obj_base.d" -Wa,-adhln="$$compile_dir/$$obj_base.lst" $1 -o $2 && \
	cp $2 "$$compile_dir/$$obj_base$(OBJEXT)"
endef
else
define COMPILEXX
	@echo "CXX: $1"
	$(Q) $(CXX) -c $(CXXFLAGS) $1 -o $2
endef
endif

# ASSEMBLE - Default macro to assemble one assembly language file
# Example: $(call ASSEMBLE, in-file, out-file)
#
# NOTE that the most common toolchain, GCC, uses the compiler to assemble
# files because this has the advantage of running the C Pre-Processor against
# the assembly language files.  This is not possible with other toolchains;
# platforms using those other tools should define AS and over-ride this
# definition in order to use the assembler directly.
#
# Depends on these settings defined in board-specific Make.defs file
# installed at $(TOPDIR)/Make.defs:
#
#   CC - By default, the C compiler is used to compile assembly language
#        files
#   AFLAGS - Options to pass to the C+compiler

ifneq ($(TIZENRT_FULL_ARTIFACTS),)
define ASSEMBLE
	@echo "AS: $1"
	$(Q) $(call TOOLCHAIN_ARTIFACT_PATHS,$1,$2); \
	$(CC) -E $(AFLAGS) $1 -o "$$assemble_dir/$$obj_base.preprocessed.s" && \
	$(CC) -c $(AFLAGS) -MMD -MP -MF "$$assemble_dir/$$obj_base.d" -Wa,-adhln="$$assemble_dir/$$obj_base.lst" $1 -o $2 && \
	cp $2 "$$assemble_dir/$$obj_base$(OBJEXT)"
endef
else
define ASSEMBLE
	@echo "AS: $1"
	$(Q) $(CC) -c $(AFLAGS) $1 -o $2
endef
endif

# MOVEOBJ - Default macro to move an object file to the correct location
# Example: $(call MOVEOBJ, prefix, directory)
#
# This is only used in directories that keep object files in sub-directories.
# Certain compilers (ZDS-II) always place the resulting files in the
# directory where the compiler was invoked with not option to generate objects
# in a different location.

define MOVEOBJ
endef

# LOCK_AR - flock command for AR to prevent concurrent archive operations
#
# This prevents multiple ar commands from running simultaneously which can cause
# corruption of archive files during parallel builds.

LOCK_AR = flock /tmp/ar.lock

# ARCHIVE - Add a list of files to an archive
# Example: $(call ARCHIVE, archive-file, "file1 file2 file3 ...")
#
# Note: The fileN strings may not contain spaces or  characters that may be
# interpreted strangely by the shell
#
# Depends on these settings defined in board-specific Make.defs file
# installed at $(TOPDIR)/Make.defs:
#
#   AR - The command to invoke the archiver (includes any options)
#
# Depends on this settings defined in board-specific defconfig file installed
# at $(TOPDIR)/.config:
#
#   CONFIG_WINDOWS_NATIVE - Defined for a Windows native build

ifeq ($(CONFIG_WINDOWS_NATIVE),y)
define ARCHIVE
	@echo AR: $2
	$(Q) $(LOCK_AR) $(AR) $1 $(2)
endef
else
ifneq ($(TIZENRT_FULL_ARTIFACTS),)
define ARCHIVE
	@echo "AR: $2"
	$(Q) $(LOCK_AR) $(AR) $1 $(2) || { echo "$(AR) $1 FAILED!" ; exit 1 ; }; \
	archive_file="$(strip $1)"; \
	archive_dir="$(TOOLCHAIN_ARTIFACT_ROOT)/archive"; \
	archive_key=`printf '%s\n' "$$archive_file" | sed -e 's#^\./##' -e 's#[/\\:]#_#g'`; \
	mkdir -p "$$archive_dir"; \
	{ printf 'ARCHIVE\t%s\n' "$$archive_file"; $(firstword $(AR)) t "$$archive_file"; } > "$$archive_dir/$$archive_key.contents" || { echo "$(firstword $(AR)) t $$archive_file FAILED!" ; exit 1 ; }; \
	cp "$$archive_file" "$$archive_dir/$$archive_key" || { echo "copy $$archive_file FAILED!" ; exit 1 ; }
endef
else
define ARCHIVE
	@echo "AR: $2"
	$(Q) $(LOCK_AR) $(AR) $1 $(2) || { echo "$(AR) $1 FAILED!" ; exit 1 ; }
endef
endif
endif

# PRELINK - Prelink a list of files
# This is useful when files were compiled with fvisibility=hidden.
# Any symbol which was not explicitly made global is invisible outside the
# prelinked file.
#
# Example: $(call PRELINK, prelink-file, "file1 file2 file3 ...")
#
# Note: The fileN strings may not contain spaces or  characters that may be
# interpreted strangely by the shell
#
# Depends on these settings defined in board-specific Make.defs file
# installed at $(TOPDIR)/Make.defs:
#
#   LD - The command to invoke the linker (includes any options)
#    OBJCOPY - The command to invoke the object cop (includes any options)
#
# Depends on this settings defined in board-specific defconfig file installed
# at $(TOPDIR)/.config:
#
#   CONFIG_WINDOWS_NATIVE - Defined for a Windows native build

ifeq ($(CONFIG_WINDOWS_NATIVE),y)
define PRELINK
	@echo PRELINK: $1
	$(Q) $(LD) -Ur -o $1 $2 && $(OBJCOPY) --localize-hidden $1
endef
else
define PRELINK
	@echo "PRELINK: $1"
	$(Q) $(LD) -Ur -o $1 $2 && $(OBJCOPY) --localize-hidden $1
endef
endif

# DELFILE - Delete one file

ifeq ($(CONFIG_WINDOWS_NATIVE),y)
define DELFILE
	$(Q) if exist $1 (del /f /q $1)
endef
else
define DELFILE
	$(Q) rm -f $1
endef
endif

# DELDIR - Delete one directory

ifeq ($(CONFIG_WINDOWS_NATIVE),y)
define DELDIR
	$(Q) if exist $1 (rmdir /q /s $1)
endef
else
define DELDIR
	$(Q) rm -rf $1
endef
endif

# MOVEFILE - Move one file

ifeq ($(CONFIG_WINDOWS_NATIVE),y)
define MOVEFILE
	$(Q) if exist $1 (move /Y $1 $2)
endef
else
define MOVEFILE
	$(Q) mv -f $1 $2
endef
endif

# CLEAN - Default clean target

ifeq ($(CONFIG_WINDOWS_NATIVE),y)
define CLEAN
	$(Q) if exist *$(OBJEXT) (del /f /q *$(OBJEXT))
	$(Q) if exist *$(LIBEXT) (del /f /q *$(LIBEXT))
	$(Q) if exist *~ (del /f /q *~)
	$(Q) if exist (del /f /q  .*.swp)
endef
else
define CLEAN
	$(Q) rm -f *$(OBJEXT) *$(LIBEXT) *~ .*.swp
endef
endif
