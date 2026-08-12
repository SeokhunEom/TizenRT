#!/bin/sh
############################################################################
# apps/examples/ltp/ltp_register.sh
#
# Discovers LTP test files containing main() and generates TASH command
# registry entries (.mdat and .pdat files) in the builtin registry.
#
# This script is called from the Makefile during the context phase,
# after the bundled LTP source has been extracted and patched.
#
# For each test file, it generates:
#   <funcname>.mdat: { "cmdname", ltp_runner_main, ... },
#   <funcname>.pdat: int ltp_runner_main(int argc, char *argv[]);
# The runner dispatches the command to the renamed function through the
# generated ltp_registry.inc mapping.
#
# The function name is derived from the file path:
#   .../condvar/pthread_cond_wait_1.c -> ltp_condvar_pthread_cond_wait_1_main
#   .../schedule/1-1.c                -> ltp_schedule_1_1_main
#
############################################################################

set -e

TEST_SUBDIR="${LTP_TEST_SUBDIR}"
REGISTRY="${BUILTIN_REGISTRY}"
STACKSIZE="${STACKSIZE:-8192}"
PRIORITY="${PRIORITY:-100}"
EXECTYPE="${THREADEXEC:-TASH_EXECMD_ASYNC}"

# LTP_TEST_SUBDIR may contain multiple space-separated directories.
# Verify at least one exists.
found_dir=0
for d in ${TEST_SUBDIR}; do
    if [ -d "${d}" ]; then
        found_dir=1
    fi
done
if [ ${found_dir} -eq 0 ]; then
    echo "ltp_register: no test directories found in: ${TEST_SUBDIR}"
    exit 0
fi

# Find all .c files in the test subdirectories (unquoted for multiple paths)
ORIGS=$(find ${TEST_SUBDIR} -name "*.c" 2>/dev/null | sort)

if [ -z "${ORIGS}" ]; then
    echo "ltp_register: no .c files found in test directories"
    exit 0
fi

# Blacklist patterns - files containing these are excluded
# Use a newline-separated list for POSIX sh compatibility
BLACKWORDS="pthread_mutexattr_setprioceiling
pthread_mutexattr_getprioceiling
pthread_getattr_np
pthread_mutex_getprioceiling
CHILD_MAX
setpgid(
PTHREAD_SCOPE_PROCESS
setpgrp
threads_scenarii.c
ucontext.h
msync
lfind
_POSIX_SPORADIC_SERVER
sched_setscheduler/19-
sched_setparam/25-
sys/shm.h
shmget
shmat
fork(
getuid
setuid
geteuid
seteuid
setgid
setpwent
getpwent
endpwent"

# Explicitly blacklisted source files (by path suffix).
# TizenRT does not support SCHED_OTHER policy (only SCHED_FIFO and SCHED_RR).
# Cannot use "SCHED_OTHER" as a content grep pattern because it would also
# exclude passing tests and error-handling tests that merely reference
# SCHED_OTHER as an invalid policy.
BLACKSRCS="conformance/interfaces/pthread_attr_setschedpolicy/1-1.c
conformance/interfaces/pthread_attr_setschedpolicy/5-1.c
conformance/interfaces/sched_get_priority_max/1-4.c
conformance/interfaces/sched_get_priority_min/1-4.c"

# pthread_attr_setschedpolicy/2-1.c requires set_affinity_single() which
# is only implemented for SMP builds (see patch 0015). Blacklist on non-SMP.
if [ "${CONFIG_SMP}" != "y" ]; then
    BLACKSRCS="${BLACKSRCS}
conformance/interfaces/pthread_attr_setschedpolicy/2-1.c"
fi





# TizenRT uses CONFIG_DISABLE_PTHREAD=n to enable pthread (inverse convention).
# If pthread is disabled, blacklist all pthread tests.
if [ "${CONFIG_DISABLE_PTHREAD}" = "y" ]; then
    BLACKWORDS="${BLACKWORDS}
pthread"
fi

# Note: Signal, mqueue, and timer tests are not included in the initial
# port (only functional/threads tests are). When expanding to more test
# categories, add conditional blacklist entries here based on TizenRT's
# CONFIG_DISABLE_* options.


# Filter out blacklisted files
FILTERED=""
for f in ${ORIGS}; do
    blacklisted=0
    # Check each blacklist word
    echo "${BLACKWORDS}" | while IFS= read -r word; do
        if grep -q "${word}" "${f}" 2>/dev/null; then
            echo "BLACKLISTED:${f}"
        fi
    done > /tmp/ltp_blacklist_tmp
    if grep -q "BLACKLISTED:${f}" /tmp/ltp_blacklist_tmp 2>/dev/null; then
        blacklisted=1
    fi
    rm -f /tmp/ltp_blacklist_tmp
    # Check against explicitly blacklisted source files (BLACKSRCS)
    if [ ${blacklisted} -eq 0 ]; then
        echo "${BLACKSRCS}" | while IFS= read -r pat; do
            [ -z "${pat}" ] && continue
            case "${f}" in
                *${pat}) echo "BLACKLISTED:${f}" ;;
            esac
        done > /tmp/ltp_blacksrc_tmp
        if grep -q "BLACKLISTED:${f}" /tmp/ltp_blacksrc_tmp 2>/dev/null; then
            blacklisted=1
        fi
        rm -f /tmp/ltp_blacksrc_tmp
    fi
    if [ ${blacklisted} -eq 0 ]; then
        if [ -z "${FILTERED}" ]; then
            FILTERED="${f}"
        else
            FILTERED="${FILTERED} ${f}"
        fi
    fi
done

# Find files containing main()
MAINCSRCS=""
for f in ${FILTERED}; do
    if grep -q "main(" "${f}" 2>/dev/null; then
        if [ -z "${MAINCSRCS}" ]; then
            MAINCSRCS="${f}"
        else
            MAINCSRCS="${MAINCSRCS} ${f}"
        fi
    fi
done

if [ -z "${MAINCSRCS}" ]; then
    echo "ltp_register: no test files with main() found"
    exit 0
fi

# Count tests
test_count=$(echo ${MAINCSRCS} | wc -w)
echo "ltp_register: registering TASH commands for ${test_count} tests..."

# Generate the table consumed by ltp_runner.c.  The runner launches each test
# as a child task so waitpid() can expose the POSIX test exit status.
RUNNER_REGISTRY="${CURDIR}/ltp_registry.inc"
TEST_MANIFEST="${CURDIR}/ltp_manifest.tsv"
: > "${RUNNER_REGISTRY}"
: > "${TEST_MANIFEST}"

# Generate .mdat and .pdat files for each test
# TASH command names are limited to 15 chars (TASH_CMD_MAXSTRLENGTH - 1).
# Use indexed short names (ltp_t1, ltp_t2, ...) for the command name,
# while keeping descriptive function names for the C entry points.
idx=1
for f in ${MAINCSRCS}; do
    # Derive function name from file path (no length limit for C symbols)
    # Example: .../condvar/pthread_cond_wait_1.c -> ltp_condvar_pthread_cond_wait_1_main
    funcname=$(echo "${f}" | awk -F "[/]" '{print "ltp_"$(NF-1)"_"$NF}' | sed 's/\.c$//' | sed 's/-/_/g')_main

    # Short indexed command name (fits in 15 chars: ltp_tXXX)
    cmdname="ltp_t${idx}"
    idx=$((idx + 1))

    # Generate .mdat file (command metadata)
    echo "{ \"${cmdname}\", ltp_runner_main, ${EXECTYPE}, ${PRIORITY}, ${STACKSIZE} }," > "${REGISTRY}/${funcname}.mdat"

    # Generate .pdat file (function prototype)
    echo "int ltp_runner_main(int argc, char *argv[]);" > "${REGISTRY}/${funcname}.pdat"

    # Generate the command-to-test entry mapping used by ltp_runner.c.
    echo "LTP_TEST(\"${cmdname}\", ${funcname})" >> "${RUNNER_REGISTRY}"

    # Record the exact source selected for reproducible host-side reporting.
    source_rel=${f#${CURDIR}/}
    printf '%s\t%s\tapps/examples/ltp/%s\n' \
        "${cmdname}" "${funcname}" "${source_rel}" >> "${TEST_MANIFEST}"

    echo "  Registered: ${cmdname} -> ${funcname}"
done

# Touch the .updated file to trigger registry rebuild
touch "${REGISTRY}/.updated"

echo "ltp_register: done."
