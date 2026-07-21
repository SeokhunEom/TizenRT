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
python_bin=/opt/homebrew/bin/python3
repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd -P)

mode=${1:?usage: mem_leak_checker_task6_seal.sh seal --source REV | receipt-negative}
shift

case "$mode" in
receipt-negative)
	[ "$#" -eq 0 ] || exit 64
	"$python_bin" -I -B \
		"$repo_root/tools/mem_leak_checker_task6_validate.py" \
		context --root "$repo_root"
	"$python_bin" -I -B \
		"$repo_root/tools/test_mem_leak_checker_task6_validate.py"
	printf '%s\n' 'MLC_TASK6_RECEIPT_NEGATIVE status=PASS validator=production_v2 cases=context,malformed,stale,dirty,extra,missing,type,noncanonical,misleading,codegraph'
	;;
seal)
	[ "$#" -eq 2 ] && [ "$1" = --source ] || exit 64
	"$python_bin" -I -B "$repo_root/tools/mem_leak_checker_task6_validate.py" \
		context --root "$repo_root"
	"$python_bin" -I -B \
		"$repo_root/tools/mem_leak_checker_task6_receipt.py" \
		seal --root "$repo_root" --source "$2"
	;;
*)
	exit 64
	;;
esac
