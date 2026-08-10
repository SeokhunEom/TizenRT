# LTP (Linux Test Project) for TizenRT

This directory contains a port of the [Linux Test Project (LTP)](https://github.com/linux-test-project/ltp)
POSIX conformance test suite to TizenRT, based on the [NuttX LTP integration](https://github.com/apache/nuttx/tree/master/apps/testing/ltp).

## Overview

LTP version **20230516** is downloaded, patched, and compiled into TizenRT
as TASH commands. Each test file's `main()` function is renamed to a unique
entry point (e.g., `ltp_condvar_pthread_cond_wait_1_main`) using the
`-Dmain=<funcname>` compiler flag, and registered as a TASH command.

## Current Status

This is a **minimal initial port** that includes only the
`functional/threads` test category (4 test files):

| TASH Command | Test File | Description |
|---|---|---|
| `ltp_t1` | `condvar/pthread_cond_wait_1.c` | pthread_cond_signal wakes high priority thread |
| `ltp_t2` | `condvar/pthread_cond_wait_2.c` | pthread_cond_signal wakes high priority thread (variant) |
| `ltp_t3` | `schedule/1-1.c` | pthread_barrier_wait wakes high priority thread |
| `ltp_t4` | `schedule/1-2.c` | pthread_barrier_wait wakes high priority thread (variant) |

## Build Instructions

### 1. Enable LTP in configuration

```
cd build/<board>
make menuconfig
```

Navigate to **Application Configuration → Examples → Linux Test Project (LTP) tests**
and enable it.

### 2. Build

```
make
```

**Note:** On the first build, the LTP source is downloaded and patched during
the `context` phase. The test files are discovered at Makefile parse time,
so the first build will not compile any tests. Run `make` a second time to
compile the test files. This is the same behavior as NuttX's LTP integration.

### 3. Run tests

```
TizenRT> ltp_t1
TizenRT> ltp_t2
TizenRT> ltp_t3
TizenRT> ltp_t4
```

## Directory Structure

```
apps/examples/ltp/
├── Make.defs           # Build integration (adds to CONFIGURED_APPS)
├── Makefile            # Main build logic: download, patch, compile, register
├── Kconfig             # Configuration options (EXAMPLES_LTP, stack size, priority)
├── config.h            # Feature detection macros for LTP
├── ltp_register.sh     # TASH command registration script
├── patches/            # TizenRT-specific patches (adapted from NuttX)
│   ├── 0001-pthread_rwlock_unlock-follow-linux.patch
│   ├── 0002-Use-ifdef-instead-of-if-for-__linux__.patch
│   └── ... (12 patches total)
├── .gitignore          # Ignores downloaded LTP source
└── README.md           # This file
```

## How It Works

### main() Renaming

LTP test files each contain a `main()` function. Since TizenRT links all
applications into a single binary, multiple `main()` functions would conflict.
The solution is to rename each `main()` to a unique function name using the
`-Dmain=<funcname>` compiler flag:

```bash
# Source: functional/threads/condvar/pthread_cond_wait_1.c
# Compiled with: -Dmain=ltp_condvar_pthread_cond_wait_1_main
# Result: int ltp_condvar_pthread_cond_wait_1_main(int argc, char *argv[]) { ... }
```

### TASH Command Registration

TizenRT uses a builtin registry system. The `ltp_register.sh` script
discovers test files and generates `.mdat` (metadata) and `.pdat` (prototype)
files in `apps/builtin/registry/`. These are compiled into `builtin_list.c`
and registered as TASH commands at startup.

**TASH command name limit:** TASH limits command names to 15 characters
(`TASH_CMD_MAXSTRLENGTH - 1`). Since LTP test names can be very long
(e.g., `ltp_condvar_pthread_cond_wait_1`), short indexed names (`ltp_t1`,
`ltp_t2`, ...) are used for TASH commands, while the C function names remain
descriptive.

### Blacklist

Some LTP tests use POSIX features not available in TizenRT (e.g.,
`pthread_mutexattr_setprioceiling`, `ucontext.h`). These are filtered out
via a blacklist mechanism in both the Makefile and `ltp_register.sh`.

## Expanding Test Coverage

To add more test categories, modify `LTP_TEST_SUBDIR` in the Makefile:

```makefile
# Current: functional/threads only
LTP_TEST_SUBDIR  = $(TESTDIR)/functional/threads

# Expanded: all functional tests
LTP_TEST_SUBDIR  = $(TESTDIR)/functional

# Full: all conformance interface tests
LTP_TEST_SUBDIR  = $(TESTDIR)/conformance/interfaces
```

## Patches

The 12 patches in `patches/` are adapted from NuttX's LTP integration,
with `__NuttX__` replaced by `__TIZENRT__`. They fix:

1. `pthread_rwlock_unlock` behavior on non-Linux
2. `#ifdef` instead of `#if` for `__linux__`
3. Static variable re-initialization for multiple runs
4. Test case updates for compatibility
5. `pthread_cond_timedwait` testcase fixes
6. rwlock initialization fixes
7. `pthread_kill` usleep to avoid semcount overturn
8. `sigaction` deadloop fix
9. fdcheck compatibility
10. Build warning fixes
11. `proc.h` duplicate inclusion fix
12. Build error fixes

## Configuration Options

| Option | Default | Description |
|---|---|---|
| `CONFIG_EXAMPLES_LTP` | n | Enable LTP tests |
| `CONFIG_EXAMPLES_LTP_STACKSIZE` | 8192 | Stack size for LTP test tasks |
| `CONFIG_EXAMPLES_LTP_PRIORITY` | 100 | Priority for LTP test tasks |
| `CONFIG_DISABLE_PTHREAD` | n | Must be `n` (pthread enabled) for LTP tests |
| `CONFIG_BUILTIN_APPS` | y | Required for TASH command registration |
