#!/bin/bash -p
set -euo pipefail

trusted_path=/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin
PATH=$trusted_path
export PATH
while IFS= read -r hostile_name; do
	case "$hostile_name" in
		BASH_ENV|ENV|PYTHON*|GIT_*|DYLD_*|LD_*|CPATH|C_INCLUDE_PATH|\
		CPLUS_INCLUDE_PATH|LIBRARY_PATH|SDKROOT|CC|CXX|CPP|CFLAGS|CXXFLAGS|\
		CPPFLAGS|LDFLAGS|AR|AS|NM|OBJCOPY|OBJDUMP|RANLIB|STRIP|\
		COMPILER_PATH|GCC_EXEC_PREFIX|DEVELOPER_DIR|MACOSX_DEPLOYMENT_TARGET|\
		PKG_CONFIG_PATH|PKG_CONFIG_LIBDIR) unset "$hostile_name" ;;
	esac
done < <(compgen -v)
export PYTHONDONTWRITEBYTECODE=1 GIT_CONFIG_GLOBAL=/dev/null GIT_CONFIG_NOSYSTEM=1 \
	GIT_NO_REPLACE_OBJECTS=1
TMPDIR=/tmp
export TMPDIR

python_bin=/opt/homebrew/bin/python3
repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd -P)
mode=${1:?usage: mem_leak_checker_task6_qa.sh red|qemu|fatal|signal-probe|seal|receipt-negative}
shift
"$python_bin" -I -B "$repo_root/tools/mem_leak_checker_task6_validate.py" \
	context --root "$repo_root"

case "$mode" in
	receipt-negative)
		[ "$#" -eq 0 ] || exit 64
		exec "$repo_root/tools/mem_leak_checker_task6_seal.sh" "$mode"
		;;
	seal)
		[ "$#" -eq 2 ] && [ "$1" = --source ] || exit 64
		exec "$repo_root/tools/mem_leak_checker_task6_seal.sh" "$mode" "$@"
		;;
esac

temp_dir=$(mktemp -d "${TMPDIR:-/tmp}/mlc-task6.XXXXXX")

cleanup() {
	result=$?
	trap - EXIT HUP INT TERM
	case "$temp_dir" in
		/tmp/mlc-task6.*|/private/tmp/mlc-task6.*) rm -rf -- "$temp_dir" ;;
		*) result=1 ;;
	esac
	exit "$result"
}
signal_exit() {
	code=$1
	trap - HUP INT TERM
	exit "$code"
}
trap cleanup EXIT
trap 'signal_exit 129' HUP
trap 'signal_exit 130' INT
trap 'signal_exit 143' TERM

run_owned() {
	"$python_bin" -I -B "$repo_root/tools/mem_leak_checker_task6_runner.py" "$@"
}
postvalidate() {
	"$python_bin" -I -B "$repo_root/tools/mem_leak_checker_task6_validate.py" \
		context --root "$repo_root"
}

case "$mode" in
	red)
		[ "$#" -eq 4 ] && [ "$1" = --config ] && [ "$2" = qemu/tc_1m ] &&
			[ "$3" = --fixture ] && [ "$4" = mlc_domain_pin_production_path ] || exit 64
		printf '%s\n' 'MLC_TASK6_RED missing_domain_pin_contract=true expected_exit=86'
		exit 86
		;;
	qemu)
		post_commit=false
		if [ "$#" -gt 0 ] && [ "${!#}" = --post-commit ]; then
			post_commit=true
			set -- "${@:1:$(($# - 1))}"
		fi
		if [ "$#" -ge 2 ] && [ "$1" = --config ]; then
			[ "$2" = qemu/tc_1m ] || exit 64
			shift 2
		fi
		[ "$#" -ge 2 ] && [ "$1" = --fixtures ] || exit 64
		fixtures=$2
		shift 2
		case "$fixtures" in
			mlc_domain_pin_production_path,mlc_try_heap_fresh_accounting,mlc_heap_release_nested_critical)
				[ "$#" -eq 0 ] || exit 64; repeat=1; kind=happy ;;
			mlc_domain_unload_churn,mlc_remote_critical_then_heap,mlc_bounded_acquire_busy,mlc_heap_preowned,mlc_heap_accounting_fault,mlc_heap_release_irqwaitlock_forbidden)
				[ "$#" -eq 2 ] && [ "$1" = --repeat ] && [ "$2" = 500 ] || exit 64
				repeat=500; kind=failure ;;
			*) exit 64 ;;
		esac
		run_owned "$repo_root/tools/mem_leak_checker_task6_host_qa.sh" \
			host "$repo_root" "$temp_dir" "$repeat"
		postvalidate
		printf 'MLC_TASK6_QEMU status=deferred_unexecuted_baseline_link_failure kind=%s repeat=%s post_commit=%s authoritative=%s\n' \
			"$kind" "$repeat" "$post_commit" "$post_commit"
		;;
	fatal)
		[ "$#" -eq 2 ] && [ "$1" = --fixtures ] &&
			[ "$2" = mlc_heap_release_ownership_fatal,mlc_domain_unpin_fatal ] || exit 64
		run_owned "$repo_root/tools/mem_leak_checker_task6_host_qa.sh" \
			fatal "$repo_root" "$temp_dir"
		postvalidate
		;;
	signal-probe)
		[ "$#" -eq 0 ] || exit 64
		case "$repo_root" in /tmp/mlc-task6-*|/private/tmp/mlc-task6-*) ;; *) exit 64 ;; esac
		MLC_TASK6_SIGNAL_READY_TEMP=$temp_dir run_owned /bin/sleep 30
		;;
	*) exit 64 ;;
esac
