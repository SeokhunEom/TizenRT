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

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)
source_root=$(CDPATH= cd -- "$script_dir/.." && pwd -P)
python_runner="$script_dir/mem_leak_checker_qemu_qa.py"

fail() {
	printf '%s\n' "mem-leak-checker QA failed: $*" >&2
	exit 1
}

usage_fail() {
	printf '%s\n' "mem-leak-checker QA usage error: $*" >&2
	exit 64
}

value_after() {
	name=$1
	shift
	while [ "$#" -gt 0 ]; do
		if [ "$1" = "$name" ]; then
			[ "$#" -ge 2 ] || usage_fail "missing value for $name"
			printf '%s\n' "$2"
			return
		fi
		shift
	done
	fail "missing required option $name"
}

task2_receipt() {
	action=$1
	fixtures=$2
	command_line=$3
	[ -z "${MLC_TASK2_RED_RECEIPT:-}" ] || fail "task-2 receipt path override is forbidden"
	python3 - "$action" "$source_root" "$fixtures" "$command_line" <<'PY'
import hashlib
import json
import os
import pathlib
import stat
import subprocess
import sys
import tempfile

action, root_text, fixtures, command_line = sys.argv[1:]
root = pathlib.Path(root_text)
BASELINE = "c93078ab05bb6463467669fb6ee19bb75ee7eaba"
TODO_BASE = "fe40a82f5dfe989c28e6da7ee64eb919e00149d9"
MAIN_BRANCH = "codex/mem-leak-checker-hardening"
TODO_BRANCH = "codex/todo2-review-blockers"
WORK_ID = "mem-leak-checker-hardening-257754dc"
PLAN = ".omo/plans/mem-leak-checker-hardening.md"
SESSION = "codex:257754dc-1fe6-461f-86d4-57a7d2b9fb8d"
EVIDENCE_RELATIVE = ".omo/start-work/artifacts/task-2-executor"
RECEIPT_RELATIVE = EVIDENCE_RELATIVE + "/task-2-red.json"

def git(repository, *args):
    return subprocess.run(
        ["git", "-C", str(repository), *args], check=True, capture_output=True
    ).stdout

def sha256(data):
    return hashlib.sha256(data).hexdigest()

def open_directory_chain(path):
    if not path.is_absolute():
        raise ValueError("trusted directory path must be absolute")
    flags = os.O_RDONLY | os.O_DIRECTORY | getattr(os, "O_NOFOLLOW", 0)
    descriptor = os.open("/", flags)
    try:
        for name in path.parts[1:]:
            next_descriptor = os.open(name, flags, dir_fd=descriptor)
            os.close(descriptor)
            descriptor = next_descriptor
        return descriptor
    except BaseException:
        os.close(descriptor)
        raise

def open_relative_directory(root_path, relative):
    descriptor = open_directory_chain(root_path)
    flags = os.O_RDONLY | os.O_DIRECTORY | getattr(os, "O_NOFOLLOW", 0)
    try:
        for name in pathlib.PurePosixPath(relative).parts:
            if name in {"", ".", ".."}:
                raise ValueError("unsafe relative directory component")
            next_descriptor = os.open(name, flags, dir_fd=descriptor)
            os.close(descriptor)
            descriptor = next_descriptor
        return descriptor
    except BaseException:
        os.close(descriptor)
        raise

def ensure_relative_directory(root_path, relative):
    descriptor = open_directory_chain(root_path)
    flags = os.O_RDONLY | os.O_DIRECTORY | getattr(os, "O_NOFOLLOW", 0)
    try:
        for name in pathlib.PurePosixPath(relative).parts:
            if name in {"", ".", ".."}:
                raise ValueError("unsafe relative directory component")
            try:
                next_descriptor = os.open(name, flags, dir_fd=descriptor)
            except FileNotFoundError:
                os.mkdir(name, 0o700, dir_fd=descriptor)
                os.fsync(descriptor)
                next_descriptor = os.open(name, flags, dir_fd=descriptor)
            os.close(descriptor)
            descriptor = next_descriptor
        os.fsync(descriptor)
        return descriptor
    except BaseException:
        os.close(descriptor)
        raise

def read_relative_file(root_path, relative, limit=4 * 1024 * 1024):
    parts = pathlib.PurePosixPath(relative).parts
    if not parts or any(name in {"", ".", ".."} for name in parts):
        raise ValueError("unsafe relative file path")
    directory = open_relative_directory(root_path, pathlib.PurePosixPath(*parts[:-1]))
    flags = os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0)
    try:
        descriptor = os.open(parts[-1], flags, dir_fd=directory)
        try:
            opened = os.fstat(descriptor)
            if not stat.S_ISREG(opened.st_mode):
                raise ValueError("trusted path is not a regular file")
            chunks = []
            total = 0
            while True:
                chunk = os.read(descriptor, min(65536, limit + 1 - total))
                if not chunk:
                    break
                chunks.append(chunk)
                total += len(chunk)
                if total > limit:
                    raise ValueError("trusted file exceeds size limit")
            named = os.stat(parts[-1], dir_fd=directory, follow_symlinks=False)
            if (opened.st_dev, opened.st_ino) != (named.st_dev, named.st_ino):
                raise ValueError("trusted file identity drift")
            return b"".join(chunks)
        finally:
            os.close(descriptor)
    finally:
        os.close(directory)

def parse_worktrees():
    text = git(root, "worktree", "list", "--porcelain").decode("utf-8", "strict")
    records = []
    for block in text.strip().split("\n\n"):
        record = {}
        for line in block.splitlines():
            key, _, value = line.partition(" ")
            record[key] = value
        records.append(record)
    return records

records = parse_worktrees()
main_records = [item for item in records if item.get("branch") == "refs/heads/" + MAIN_BRANCH]
todo_records = [item for item in records if item.get("branch") == "refs/heads/" + TODO_BRANCH]
if len(main_records) != 1 or len(todo_records) != 1:
    raise SystemExit("exact main/Todo2 worktree registration mismatch")
main_root = pathlib.Path(main_records[0]["worktree"])
if pathlib.Path(todo_records[0]["worktree"]) != root or main_root == root:
    raise SystemExit("Todo2 isolated worktree identity mismatch")

try:
    boulder = json.loads(read_relative_file(main_root, ".omo/boulder.json"))
except (OSError, ValueError, UnicodeDecodeError, json.JSONDecodeError) as error:
    raise SystemExit("invalid descriptor-bound Boulder v2 context") from error
expected_work = {
    "work_id": WORK_ID,
    "active_plan": PLAN,
    "plan_name": "mem-leak-checker-hardening",
    "session_ids": [SESSION],
    "status": "active",
    "worktree_path": str(main_root),
}
if boulder != {
    "schema_version": 2,
    "active_work_id": WORK_ID,
    "works": {WORK_ID: expected_work},
}:
    raise SystemExit("Boulder v2 active context mismatch")
read_relative_file(main_root, PLAN)
baseline = read_relative_file(
    main_root, ".omo/start-work/artifacts/task-1-executor/baseline.sha"
).decode("ascii", "strict").strip()
if baseline != BASELINE or git(main_root, "rev-parse", BASELINE).decode().strip() != BASELINE:
    raise SystemExit("durable baseline mismatch")
main_head = git(main_root, "rev-parse", "HEAD").decode().strip()
main_branch = git(main_root, "branch", "--show-current").decode().strip()
todo_head = git(root, "rev-parse", "HEAD").decode().strip()
todo_branch = git(root, "branch", "--show-current").decode().strip()
if main_branch != MAIN_BRANCH or main_records[0].get("HEAD") != main_head:
    raise SystemExit("active main worktree branch/HEAD mismatch")
if todo_branch != TODO_BRANCH or todo_records[0].get("HEAD") != todo_head:
    raise SystemExit("Todo2 worktree branch/HEAD mismatch")
if git(root, "rev-parse", TODO_BASE).decode().strip() != TODO_BASE:
    raise SystemExit("Todo2 base commit missing")
if git(root, "merge-base", TODO_BASE, todo_head).decode().strip() != TODO_BASE:
    raise SystemExit("Todo2 branch is not rooted at the required base")
if subprocess.run(
    ["git", "-C", str(main_root), "merge-base", "--is-ancestor", BASELINE, main_head]
).returncode != 0:
    raise SystemExit("active main HEAD is outside baseline history")
evidence_descriptor = ensure_relative_directory(root, EVIDENCE_RELATIVE)
os.close(evidence_descriptor)

scenario = json.loads(read_relative_file(root, "tools/mem_leak_checker_scenarios/task-2.json"))
red_case = scenario["red"][0]
expected_command = red_case["command"]
expected_fixtures = red_case["fixtures"]
expected_exit = red_case["expected_exit"]
fixture_paths = sorted([
    "apps/examples/testcase/le_tc/kernel/Make.defs",
    "apps/examples/testcase/le_tc/kernel/tc_mem_leak_checker.c",
    "apps/examples/testcase/le_tc/kernel/tc_mem_leak_checker_realloc.c",
    "os/include/tinyara/mm/mm_alloc_padding.h",
    "os/mm/mm_heap/mm_realloc_logic.h",
    "os/mm/umm_heap/umm_malloc.h",
    "tools/mem_leak_checker_alloc_bounds_qa.sh",
    "tools/mem_leak_checker_contracts/task-2.json",
    "tools/mem_leak_checker_scenarios/task-2.json",
    "tools/tests/mem_leak_checker_alloc_bounds_model.c",
    "tools/tests/mem_leak_checker_alloc_layout_characterization.c",
    "tools/tests/mem_leak_checker_alloc_padding_unrepresentable.c",
    "tools/tests/mem_leak_checker_realloc_mutation_harness.c",
])

entries = []
for relative in fixture_paths:
    data = read_relative_file(root, relative)
    entries.append({"path": relative, "sha256": sha256(data)})
digest_input = "".join(f"{entry['sha256']}  {entry['path']}\n" for entry in entries).encode()
identity = {
    "baseline_sha": baseline,
    "todo_base_sha": TODO_BASE,
    "current_head": todo_head,
    "main_head": main_head,
    "main_worktree": str(main_root),
    "main_branch": main_branch,
    "work_id": WORK_ID,
    "session_id": SESSION,
    "active_plan": PLAN,
    "todo_worktree": str(root),
    "todo_branch": todo_branch,
    "fixture_digest": sha256(digest_input),
    "fixture_files": entries,
    "fixture_patch_sha256": sha256(git(root, "diff", "--binary", "HEAD", "--", *fixture_paths)),
    "full_patch_sha256": sha256(git(root, "diff", "--binary", "HEAD")),
    "staged_write_tree": git(root, "write-tree").decode().strip(),
}

def validate_document(document):
    if document.get("task") != 2 or document.get("kind") != "development-red":
        raise ValueError("invalid task-2 RED receipt kind")
    if document.get("fixtures") != expected_fixtures:
        raise ValueError("task-2 RED receipt fixture selection drift")
    if document.get("command") != expected_command:
        raise ValueError("task-2 RED receipt command drift")
    if document.get("exit") != expected_exit:
        raise ValueError("task-2 RED receipt exit drift")
    for key, value in identity.items():
        if document.get(key) != value:
            raise ValueError(f"task-2 RED receipt drift: {key}")

if action == "create":
    if fixtures != expected_fixtures or command_line != expected_command:
        raise SystemExit("RED invocation does not match canonical task-2 scenario")
    document = {
        "schema": 1,
        "task": 2,
        "kind": "development-red",
        "fixtures": fixtures,
        "command": command_line,
        "exit": expected_exit,
        **identity,
    }
    encoded = (json.dumps(document, sort_keys=True, separators=(",", ":")) + "\n").encode()
    try:
        existing = read_relative_file(root, RECEIPT_RELATIVE)
    except FileNotFoundError:
        existing = None
    if existing is not None:
        if existing != encoded:
            raise SystemExit("existing RED receipt identity differs")
    else:
        flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL
        if hasattr(os, "O_NOFOLLOW"):
            flags |= os.O_NOFOLLOW
        directory_fd = open_relative_directory(root, EVIDENCE_RELATIVE)
        fd = os.open("task-2-red.json", flags, 0o600, dir_fd=directory_fd)
        try:
            view = memoryview(encoded)
            while view:
                try:
                    written = os.write(fd, view)
                except InterruptedError:
                    continue
                if written <= 0:
                    raise OSError("short write while publishing task-2 RED receipt")
                view = view[written:]
            os.fsync(fd)
            published = os.fstat(fd)
            owned_identity = (published.st_dev, published.st_ino)
            named = os.stat("task-2-red.json", dir_fd=directory_fd, follow_symlinks=False)
            if (named.st_dev, named.st_ino) != owned_identity:
                raise OSError("task-2 RED receipt identity drift during publication")
        except BaseException:
            try:
                opened = os.fstat(fd)
                owned_identity = (opened.st_dev, opened.st_ino)
                named = os.stat("task-2-red.json", dir_fd=directory_fd, follow_symlinks=False)
                if (named.st_dev, named.st_ino) == owned_identity:
                    os.unlink("task-2-red.json", dir_fd=directory_fd)
                    os.fsync(directory_fd)
            except FileNotFoundError:
                pass
            raise
        finally:
            os.close(fd)
        try:
            os.fsync(directory_fd)
        finally:
            os.close(directory_fd)
elif action in {"verify", "negative"}:
    receipt_bytes = read_relative_file(root, RECEIPT_RELATIVE)
    document = json.loads(receipt_bytes)
    try:
        validate_document(document)
        fixture_mutant = dict(document)
        fixture_mutant["fixtures"] = "mlc_alloc_zero,mlc_alloc_bounds"
        command_mutant = dict(document)
        command_mutant["command"] = expected_command + " --repeat 2"
        for mutant in (fixture_mutant, command_mutant):
            try:
                validate_document(mutant)
            except ValueError:
                continue
            raise ValueError("task-2 RED receipt mutation unexpectedly accepted")
        wrong_branch = dict(document)
        wrong_branch["todo_branch"] = "codex/wrong-todo2"
        wrong_baseline = dict(document)
        wrong_baseline["baseline_sha"] = TODO_BASE
        for mutant in (wrong_branch, wrong_baseline):
            try:
                validate_document(mutant)
            except ValueError:
                continue
            raise ValueError("task-2 context mutation unexpectedly accepted")
        with tempfile.TemporaryDirectory(prefix="mlc-task2-symlink-") as directory:
            os.symlink(
                str(root / RECEIPT_RELATIVE),
                pathlib.Path(directory) / "receipt.json",
            )
            try:
                read_relative_file(pathlib.Path(directory), "receipt.json")
            except OSError:
                pass
            else:
                raise ValueError("symlink receipt unexpectedly accepted")
    except ValueError as error:
        raise SystemExit(str(error)) from error
else:
    raise SystemExit("invalid receipt action")

print(sha256(read_relative_file(root, RECEIPT_RELATIVE)))
PY
}

task2_validate_fixtures() {
	requested=$1
	[ -n "$requested" ] || usage_fail "empty task-2 fixture list"
	case "$requested" in
		,*|*,|*,,*) usage_fail "malformed task-2 fixture list: $requested" ;;
	esac
	old_ifs=$IFS
	IFS=,
	for fixture in $requested; do
		case "$fixture" in
			mlc_alloc_bounds|mlc_alloc_zero|mlc_alloc_padding_invalid|mlc_alloc_padding_unrepresentable) ;;
			*) IFS=$old_ifs; fail "unknown task-2 fixture: $fixture" ;;
		esac
	done
	IFS=$old_ifs
}

run_audit() {
	python3 - "$source_root" <<'PY'
import ast
import json
import pathlib
import re
import subprocess
import sys

root = pathlib.Path(sys.argv[1])
python_paths = [
    root / "tools/mem_leak_checker_qemu_qa.py",
    root / "tools/mem_leak_checker_qa_core.py",
    root / "tools/mem_leak_checker_preflight.py",
]
for path in python_paths:
    compile(path.read_text(encoding="utf-8"), str(path), "exec")
contract = json.loads((root / "tools/mem_leak_checker_contracts/task-1.json").read_text())
golden = json.loads((root / "tools/mem_leak_checker_goldens/task-1-legacy.txt").read_text())
assert isinstance(golden, str)
assert contract["abi"] == {
    "argv_behavior": "mem_leak_checker_main ignores argc and argv",
    "entry_signatures": [
        "int run_mem_leak_checker(int checker_pid, char *bin_name)",
        "int run_all_mem_leak_checker(int checker_pid)",
    ],
    "selector": "PR_MEM_LEAK_CHECKER",
}
assert contract["return_codes"] == {"error": "ERROR", "success": "OK"}
task2_contract = json.loads((root / "tools/mem_leak_checker_contracts/task-2.json").read_text())
task2_scenarios = json.loads((root / "tools/mem_leak_checker_scenarios/task-2.json").read_text())
assert task2_contract["requested_extent_consumers"] == {
    "status": "routed_not_implemented_in_task_2",
    "todos": {
        "3": "scanner interval construction and exact-zero precedence",
        "7": "candidate manifest requested extents",
        "11": "production snapshot adapter consumption",
    },
}
assert task2_scenarios["red"][0]["expected_exit"] == 86
assert task2_scenarios["red_exempt"] is False
source = (root / "os/kernel/debug/mem_leak_checker.c").read_text()

def function_body(text, name):
    match = re.search(rf"\b{name}\s*\([^;]*?\)\s*\{{", text, re.S)
    assert match, f"missing function body: {name}"
    depth = 1
    cursor = match.end()
    while depth:
        assert cursor < len(text), f"unterminated function body: {name}"
        depth += (text[cursor] == "{") - (text[cursor] == "}")
        cursor += 1
    return text[match.end():cursor - 1]

def printf_formats(text, name):
    body = function_body(text, name)
    calls = re.finditer(r'printf\s*\(\s*((?:"(?:\\.|[^"\\])*"\s*)+)', body, re.S)
    return tuple(
        "".join(ast.literal_eval(token) for token in re.findall(r'"(?:\\.|[^"\\])*"', call.group(1)))
        for call in calls
    )

dump_formats = printf_formats(source, "print_mem_hex_dump")
info_formats = printf_formats(source, "print_heap_report")
all_formats = printf_formats(source, "run_all_mem_leak_checker_with_capture")
dump_limit = re.search(r"^#define MEM_DUMP_MAX_BYTES ([0-9]+)$", source, re.M)
assert dump_limit and int(dump_limit.group(1)) == 32
dump_bytes = int(dump_limit.group(1))
assert dump_formats == ("[DATA] ", "%02x ", "\n       ", "\n")
assert info_formats == (
    "Type   |    Addr    | Size(byte) |    Owner   | PID \n",
    "---------------------------------------------------\n",
    "LEAK   | %10p |  %8d  | %10p | %d\n",
    "BROKEN | %p\n",
    "*** %d LEAKS, %d BROKENS.\n",
    "*** NO MEMORY LEAK.\n",
)
assert all_formats == (
    "\nKernel :\n",
    "\nBelow are text addresses of loadable apps (and common binary if enabled) :\n",
    "The pc value of the allocation can be obtained by subtracting the text start address of the appropriate binary\n\n",
    "[%s] Text Addr : %p, Text Size : %u\n",
    "\n",
    "%s :\n",
)

def c_format(fmt, values):
    compatible = re.sub(r"%(\d*)p", r"%\1s", fmt).replace("%u", "%d")
    return compatible % values

data = dump_formats[0]
for index in range(dump_bytes):
    data += c_format(dump_formats[1], index)
    if index == 15:
        data += dump_formats[2]
data += dump_formats[3]
transcript = all_formats[0]
transcript += info_formats[0] + info_formats[1]
transcript += c_format(info_formats[2], ("0x20001000", 64, "0x08001234", 42))
transcript += data
transcript += c_format(info_formats[3], "0x20002000")
transcript += c_format(info_formats[4], (1, 1))
transcript += all_formats[1] + all_formats[2]
transcript += c_format(all_formats[3], ("sample_app", "0x08010000", 4096))
transcript += all_formats[4] + c_format(all_formats[5], "sample_app") + info_formats[5]

leak_row = re.compile(r"^(LEAK   \| )\s*(0x[0-9a-f]+)( \|  \s*64  \| )\s*(0x[0-9a-f]+)( \| )42$", re.M)
broken_row = re.compile(r"^(BROKEN \| )(0x[0-9a-f]+)$", re.M)
text_row = re.compile(r"^(\[sample_app\] Text Addr : )(0x[0-9a-f]+)(, Text Size : 4096)$", re.M)
assert len(leak_row.findall(transcript)) == 1
assert len(broken_row.findall(transcript)) == 1
assert len(text_row.findall(transcript)) == 1
assert transcript.count("[DATA] ") == 1
assert len(re.findall(r"\b[0-9a-f]{2} ", data)) == dump_bytes
assert transcript.count("*** 1 LEAKS, 1 BROKENS.\n") == 1
assert transcript.count("*** NO MEMORY LEAK.\n") == 1
normalized = leak_row.sub(r"\1<ADDR>\3<ADDR>\5<PID>", transcript)
normalized = broken_row.sub(r"\1<ADDR>", normalized)
normalized = text_row.sub(r"\1<ADDR>\3", normalized)
assert normalized == golden

assert source.count("int run_mem_leak_checker(int checker_pid, char *bin_name)") == 1
assert source.count("int run_all_mem_leak_checker(int checker_pid)") == 1
all_body = function_body(source, "run_all_mem_leak_checker_with_capture")
assert all_body.count('run_mem_leak_checker_owned(&lifecycle, &report, &guard, checker_pid,\n\t\t"kernel", capture)') == 1
assert all_body.count("run_mem_leak_checker_owned(&lifecycle, &report, &guard,") == 2
dump_body = function_body(source, "print_mem_hex_dump")
assert dump_body.count("for (i = 0; i < dump_size; i++)") == 1
assert dump_body.count("(i + 1) % 16 == 0 && (i + 1) < dump_size") == 1
info_body = function_body(source, "print_heap_report")
leak_call = re.compile(
    r'printf\("LEAK   \| %10p \|  %8d  \| %10p \| %d\\n",\s*'
    r"row->address,\s*row->size,\s*row->owner_address,\s*row->pid\);"
)
dump_call = re.compile(r"print_mem_hex_dump\(row->dump, row->dump_size\);")
assert len(leak_call.findall(info_body)) == 1
assert len(dump_call.findall(info_body)) == 1
task_prctl = (root / "os/kernel/task/task_prctl.c").read_text()
assert task_prctl.count("case PR_MEM_LEAK_CHECKER:") == 1
assert task_prctl.count("run_all_mem_leak_checker_with_capture(checker_pid, &capture)") == 1
assert task_prctl.count("up_mem_leak_capture_current(&capture)") == 1
public_header = (root / "os/include/tinyara/mm/mm.h").read_text()
assert public_header.count("int run_all_mem_leak_checker(int checker_pid);") == 1
app_main = (root / "apps/system/mem_leak_checker/mem_leak_checker_main.c").read_text()
assert app_main.count("int mem_leak_checker_main(int argc, char **argv)") == 1
assert app_main.count("prctl(PR_MEM_LEAK_CHECKER, getpid())") == 1
PY
	git -C "$source_root" diff --check
	"$script_dir/mem_leak_checker_task12_qa.sh" audit >/dev/null
	"$script_dir/mem_leak_checker_task14_qa.sh" audit >/dev/null
}

command=${1:-}
[ -n "$command" ] || fail "missing command"
shift
case "$command" in
	context)
		exec python3 "$python_runner" context "$@"
		;;
	self-test)
		if printf '%s\n' "$*" | rg -q -- '(^| )--task 15( |$)'; then
			exec "$script_dir/mem_leak_checker_task15_qa.sh" self-test "$@"
		fi
		exec python3 "$python_runner" self-test "$@"
		;;
	static-audit)
		if [ "$(value_after --task "$@" 2>/dev/null || true)" = 15 ]; then
			exec "$script_dir/mem_leak_checker_task15_qa.sh" static-audit
		fi
		fail "static-audit is installed only for task 15"
		;;
	build)
		if [ "$(value_after --task "$@" 2>/dev/null || true)" = 15 ]; then
			exec "$script_dir/mem_leak_checker_task15_qa.sh" build "$@"
		fi
		fail "build is installed only for task 15"
		;;
	qemu)
		[ "$#" -ge 2 ] && [ "$1" = --task ] || usage_fail "qemu requires leading --task"
		task=$(value_after --task "$@")
		case "$task" in
			15)
				shift 2
				exec "$script_dir/mem_leak_checker_task15_qa.sh" qemu "$@"
				;;
			14)
				[ "$#" -eq 6 ] || [ "$#" -eq 7 ] || usage_fail "unexpected task-14 qemu options"
				fixtures=$(value_after --fixtures "$@")
				[ "$(value_after --repeat "$@")" = 100 ] || [ "$(value_after --repeat "$@")" = 500 ] || fail "task-14 repeat mismatch"
				post_commit=
				for argument in "$@"; do
					[ "$argument" != --post-commit ] || post_commit=--post-commit
				done
				case "$fixtures" in
					mlc_fault_matrix,mlc_reentrancy|mlc_fault_matrix,mlc_domain_heap_task_churn) ;;
					*) fail "unknown task-14 fixture selection: $fixtures" ;;
				 esac
				exec "$script_dir/mem_leak_checker_task14_qa.sh" qemu \
					--fixtures "$fixtures" --repeat "$(value_after --repeat "$@")" $post_commit
				;;
			13)
				[ "$#" -eq 8 ] || usage_fail "unexpected task-13 qemu options"
				[ "$(value_after --command "$@")" = kernel_tc ] || fail "task-13 command mismatch"
				[ "$(value_after --fixture-filter "$@")" = tc_mem_leak_checker ] || fail "task-13 fixture filter mismatch"
				[ "$(value_after --repeat "$@")" = 20 ] || fail "task-13 repeat mismatch"
				exec "$script_dir/mem_leak_checker_task13_qa.sh" qemu \
					--fixture-filter tc_mem_leak_checker --repeat 20
				;;
			11)
				[ "$#" -eq 7 ] || usage_fail "unexpected task-11 qemu options"
				fixtures=$(value_after --fixtures "$@")
				[ "$(value_after --repeat "$@")" = 20 ] || fail "task-11 repeat mismatch"
				[ "${7:-}" = --post-commit ] || usage_fail "task-11 requires --post-commit"
				case "$fixtures" in
					mlc_production_snapshot,mlc_admission_workspace_teardown_race|\
					mlc_production_adapter_faults,mlc_late_failure_atomic_publish,mlc_post_unpin_poison,mlc_teardown_failure_admission) ;;
					*) fail "unknown task-11 fixture selection: $fixtures" ;;
				 esac
				"$script_dir/mem_leak_checker_task11_qa.sh" host "$fixtures" "$(value_after --repeat "$@")"
				printf 'MLC_TASK11_QEMU status=deferred_unexecuted_baseline_link_failure\n'
				;;
			12)
				[ "$#" -eq 4 ] || [ "$#" -eq 5 ] || usage_fail "unexpected task-12 qemu options"
				fixtures=$(value_after --fixtures "$@")
				case "$fixtures" in
					mlc_report_contract,mlc_legacy_golden_flat,mlc_legacy_golden_loadable,mlc_ambiguous_only|\
					mlc_incomplete_output_contract,mlc_legacy_appended_field_rejected,mlc_reason_precedence) ;;
					*) fail "unknown task-12 fixture selection: $fixtures" ;;
				 esac
				post_commit=
				for argument in "$@"; do
					[ "$argument" != --post-commit ] || post_commit=--post-commit
				done
				exec "$script_dir/mem_leak_checker_task12_qa.sh" qemu \
					--fixtures "$fixtures" $post_commit
				;;
			10)
				exec "$script_dir/mem_leak_checker_task10_qa.sh" qemu "$@"
				;;
			9)
				[ "$#" -eq 5 ] || usage_fail "unexpected task-9 qemu options"
				fixtures=$(value_after --fixtures "$@")
				[ "${5:-}" = --post-commit ] || usage_fail "task-9 requires --post-commit"
				case "$fixtures" in
					mlc_cpu_pause_ownership,mlc_cpu_pause_atomic_publication,mlc_irq_waitlock_pause_poll) kind=happy ;;
					mlc_cpu_pause_cancel_races,mlc_poll_cancel_irq_restore_drain,mlc_poll_cancel_initially_masked,mlc_cpu_pause_reordering,mlc_cpu_pause_stale_ipi,mlc_irq_waitlock_late_sgi,mlc_irq_waitlock_ordinary_contention,mlc_generation_exhausted,mlc_remote_paused_counter_boundaries) kind=failure ;;
					*) fail "unknown task-9 fixture selection: $fixtures" ;;
				esac
				exec "$script_dir/mem_leak_checker_task9_qa.sh" run "$kind" "$fixtures"
				;;
			7)
				[ "$#" -eq 4 ] || [ "$#" -eq 5 ] ||
					usage_fail "unexpected task-7 qemu options"
				has_fixture=0
				has_fixtures=0
				for argument in "$@"; do
					[ "$argument" != --fixture ] || has_fixture=1
					[ "$argument" != --fixtures ] || has_fixtures=1
				done
				[ "$((has_fixture + has_fixtures))" -eq 1 ] ||
					usage_fail "task 7 requires one fixture selector"
				if [ "$has_fixture" -eq 1 ]; then
					selector=fixture
					fixtures=$(value_after --fixture "$@")
				else
					selector=fixtures
					fixtures=$(value_after --fixtures "$@")
				fi
				post_commit=false
				for argument in "$@"; do
					[ "$argument" != --post-commit ] || post_commit=true
				done
				exec "$script_dir/mem_leak_checker_task7_qa.sh" qemu \
					--selector "$selector" --fixtures "$fixtures" \
					--post-commit "$post_commit"
				;;
			6)
				shift 2
				exec "$script_dir/mem_leak_checker_task6_qa.sh" qemu "$@"
				;;
			8)
				[ "$#" -eq 7 ] || usage_fail "unexpected task-8 qemu options"
				has_fixture=0
				has_fixtures=0
				for argument in "$@"; do
					[ "$argument" != --fixture ] || has_fixture=1
					[ "$argument" != --fixtures ] || has_fixtures=1
				done
				[ "$((has_fixture + has_fixtures))" -eq 1 ] || usage_fail \
					"task 8 requires exactly one fixture selector"
				if [ "$has_fixtures" -eq 1 ]; then
					selector=fixtures
					fixtures=$(value_after --fixtures "$@")
				else
					selector=fixture
					fixtures=$(value_after --fixture "$@")
				fi
				case "$fixtures" in
					mlc_task_roots,mlc_direct_wrapper_roots|mlc_invalid_task_irq_context) ;;
					*) fail "unknown task-8 fixture selection: $fixtures" ;;
				esac
				repeat=$(value_after --repeat "$@")
				[ "$repeat" = 1 ] || fail "task-8 repeat mismatch"
				post_commit=0
				for argument in "$@"; do
					[ "$argument" != --post-commit ] || post_commit=$((post_commit + 1))
				done
				[ "$post_commit" -eq 1 ] || usage_fail "task-8 requires one --post-commit"
				exec "$script_dir/mem_leak_checker_task8_qa.sh" qemu \
					--selector "$selector" --fixtures "$fixtures" \
					--repeat "$repeat" --post-commit
				;;
			5)
				[ "$#" -eq 6 ] || [ "$#" -eq 7 ] || \
					usage_fail "unexpected task-5 qemu options"
				fixtures=$(value_after --fixtures "$@")
				repeat=$(value_after --repeat "$@")
				post_commit=
				for argument in "$@"; do
					[ "$argument" != --post-commit ] || post_commit=--post-commit
				done
				if [ "$#" -eq 7 ]; then
					[ "$post_commit" = --post-commit ] || \
						usage_fail "task-5 seventh option must be --post-commit"
				else
					[ -z "$post_commit" ] || usage_fail "duplicate task-5 --post-commit"
				fi
				exec "$script_dir/mem_leak_checker_task5_scenarios.sh" qemu \
					--fixtures "$fixtures" --repeat "$repeat" $post_commit
				;;
			1)
				evidence_dir=$("$0" context --print-evidence-dir)
				task_dir="$evidence_dir/task-1"
				[ -d "$task_dir" ] || mkdir "$task_dir"
				exec python3 "$python_runner" scenario-static --output-json "$task_dir/qemu-deferred.json"
				;;
			3)
				[ "$#" -eq 4 ] || usage_fail "unexpected task-3 qemu options"
				has_fixture=0
				has_fixtures=0
				for argument in "$@"; do
					[ "$argument" != --fixture ] || has_fixture=1
					[ "$argument" != --fixtures ] || has_fixtures=1
				done
				[ "$((has_fixture + has_fixtures))" -eq 1 ] || usage_fail \
					"task 3 requires exactly one of --fixture or --fixtures"
				if [ "$has_fixture" -eq 1 ]; then
					selector=fixture
					fixtures=$(value_after --fixture "$@")
				else
					selector=fixtures
					fixtures=$(value_after --fixtures "$@")
				fi
				exec python3 "$python_runner" task-3-deferred \
					--selector "$selector" --fixtures "$fixtures"
				;;
			4)
				exec "$script_dir/mem_leak_checker_task4_qa.sh" qemu "$@"
				;;
			2)
				if printf '%s\n' "$*" | rg -q -- '--fixtures'; then
					fixtures=$(value_after --fixtures "$@")
				else
					fixtures=$(value_after --fixture "$@")
				fi
				task2_validate_fixtures "$fixtures"
				receipt_sha=$(task2_receipt verify "$fixtures" "$0 $command $*")
				"$script_dir/mem_leak_checker_alloc_bounds_qa.sh" --fixtures "$fixtures" >/dev/null
				printf 'MLC_QA_QEMU task=2 fixtures=%s status=deferred_unexecuted_baseline_link_failure red_linkage=verified receipt_sha256=%s\n' "$fixtures" "$receipt_sha"
				;;
			*) fail "task scenario is not installed: $task" ;;
		esac
		;;
	reboot-fatal)
		if [ "$#" -eq 4 ] && [ "$1" = --task ] && [ "$2" = 14 ]; then
			fixtures=$(value_after --fixtures "$@")
			[ "$fixtures" = mlc_resume_fatal,mlc_cancel_ambiguous_fatal ] || fail "unknown task-14 fatal fixtures: $fixtures"
			exec "$script_dir/mem_leak_checker_task14_qa.sh" fatal --fixtures "$fixtures"
		fi
		[ "$#" -eq 5 ] && [ "$1" = --task ] && [ "$2" = 9 ] || \
			usage_fail "reboot-fatal requires task 9"
		fixtures=$(value_after --fixtures "$@")
		[ "${5:-}" = --post-commit ] || usage_fail "task-9 fatal requires --post-commit"
		[ "$fixtures" = mlc_resume_fatal,mlc_cancel_ambiguous_fatal,mlc_remote_paused_counter_exhausted,mlc_rtl_system_reset_closure ] || \
			fail "unknown task-9 fatal fixtures: $fixtures"
		exec "$script_dir/mem_leak_checker_task9_qa.sh" run fatal "$fixtures"
		;;
	scenario-static)
		output=$(value_after --output-json "$@")
		exec python3 "$python_runner" scenario-static --output-json "$output"
		;;
	audit)
		run_audit
		;;
	pipeline-probe)
		status=${1:-19}
		sh -c "exit $status" | sed -n '1p'
		;;
	red)
		[ "$#" -ge 2 ] && [ "$1" = --task ] || usage_fail "RED requires leading --task"
		task=$(value_after --task "$@")
		if [ "$task" = 6 ]; then
			shift 2
			exec "$script_dir/mem_leak_checker_task6_qa.sh" red "$@"
		fi
		if [ "$task" = 8 ]; then
			[ "$#" -eq 6 ] || usage_fail "unexpected task-8 RED options"
			[ "$(value_after --config "$@")" = qemu/tc_1m ] || fail "task-8 RED config mismatch"
			[ "$(value_after --fixture "$@")" = mlc_task_roots ] || fail "task-8 RED fixture mismatch"
			exec env MLC_TASK8_RED_ARMED=1 "$script_dir/mem_leak_checker_task8_qa.sh" red
		fi
		if [ "$task" = 9 ]; then
			[ "$#" -eq 6 ] || usage_fail "unexpected task-9 RED options"
			[ "$(value_after --config "$@")" = qemu/tc_1m ] || fail "task-9 RED config mismatch"
			[ "$(value_after --fixture "$@")" = mlc_cpu_pause_ownership ] || fail "task-9 RED fixture mismatch"
			exec "$script_dir/mem_leak_checker_task9_qa.sh" red
		fi
		if [ "$task" = 10 ]; then
			[ "$#" -eq 6 ] || usage_fail "unexpected task-10 RED options"
			[ "$(value_after --config "$@")" = qemu/tc_1m ] || fail "task-10 RED config mismatch"
			[ "$(value_after --fixture "$@")" = mlc_static_budget ] || fail "task-10 RED fixture mismatch"
			exec "$script_dir/mem_leak_checker_task10_qa.sh" red
		fi
		if [ "$task" = 11 ]; then
			[ "$#" -eq 6 ] || usage_fail "unexpected task-11 RED options"
			[ "$(value_after --config "$@")" = qemu/tc_1m ] || fail "task-11 RED config mismatch"
			[ "$(value_after --fixture "$@")" = mlc_production_snapshot ] || fail "task-11 RED fixture mismatch"
			printf 'MLC_TASK11_RED status=expected_failure exit=86 evidence=development_only authoritative=false\n'
			exit 86
		fi
		if [ "$task" = 12 ]; then
			[ "$#" -eq 6 ] || usage_fail "unexpected task-12 RED options"
			[ "$(value_after --config "$@")" = qemu/tc_1m ] || fail "task-12 RED config mismatch"
			[ "$(value_after --fixture "$@")" = mlc_report_contract ] || fail "task-12 RED fixture mismatch"
			exec "$script_dir/mem_leak_checker_task12_qa.sh" red
		fi
		if [ "$task" = 13 ]; then
			[ "$#" -eq 6 ] || usage_fail "unexpected task-13 RED options"
			[ "$(value_after --config "$@")" = qemu/tc_1m ] || fail "task-13 RED config mismatch"
			[ "$(value_after --fixture "$@")" = tc_mem_leak_checker ] || fail "task-13 RED fixture mismatch"
			printf '%s\n' 'MLC_TASK13_RED status=exempt evidence=host_static_only authoritative=false'
			exit 0
		fi
		if [ "$task" = 14 ]; then
			[ "$#" -eq 6 ] || usage_fail "unexpected task-14 RED options"
			[ "$(value_after --config "$@")" = qemu/tc_1m ] || fail "task-14 RED config mismatch"
			[ "$(value_after --fixtures "$@")" = mlc_fault_matrix,mlc_reentrancy ] || fail "task-14 RED fixture mismatch"
			exec "$script_dir/mem_leak_checker_task14_qa.sh" red
		fi
		if [ "$task" = 7 ]; then
			[ "$#" -eq 6 ] || usage_fail "unexpected task-7 RED options"
			[ "$(value_after --config "$@")" = qemu/tc_1m ] ||
				fail "task-7 RED config mismatch"
			[ "$(value_after --fixture "$@")" = mlc_candidate_manifest ] ||
				fail "task-7 RED fixture mismatch"
			exec "$script_dir/mem_leak_checker_task7_qa.sh" red
		fi
		if [ "$task" = 5 ]; then
			[ "$#" -eq 6 ] || usage_fail "unexpected task-5 RED options"
			config=$(value_after --config "$@")
			fixture=$(value_after --fixture "$@")
			exec "$script_dir/mem_leak_checker_task5_scenarios.sh" red \
				--config "$config" --fixture "$fixture"
		fi
		if [ "$task" = 4 ]; then
			exec "$script_dir/mem_leak_checker_task4_qa.sh" red "$@"
		fi
		if [ "$task" = 3 ]; then
			[ "$#" -eq 6 ] || usage_fail "unexpected task-3 RED options"
			config=$(value_after --config "$@")
			[ "$config" = qemu/tc_1m ] || fail "task-3 RED config mismatch"
			fixtures=$(value_after --fixtures "$@")
			exec python3 "$python_runner" task-3-red --fixtures "$fixtures"
		fi
		[ "$task" = 2 ] || fail "Todo 1 is RED-exempt"
		fixtures=$(value_after --fixtures "$@")
		task2_validate_fixtures "$fixtures"
		receipt_sha=$(task2_receipt create "$fixtures" "$0 $command $*")
		printf 'MLC_QA_RED task=2 fixtures=%s status=evidence_bound_expected_failure exit=86 receipt_sha256=%s\n' "$fixtures" "$receipt_sha"
		exit 86
		;;
	budget-audit)
		task=$(value_after --task "$@")
		if [ "$task" = 15 ]; then
			exec "$script_dir/mem_leak_checker_task15_qa.sh" budget-audit "$@"
		fi
		[ "$task" = 10 ] || fail "budget-audit is installed only for task 10"
		exec "$script_dir/mem_leak_checker_task10_qa.sh" budget-audit "$@"
		;;
	seal-all)
		exec "$script_dir/mem_leak_checker_task15_qa.sh" seal-all "$@"
		;;
	deploy-hardware|hardware|audit-hardware-unavailable|release-build-worktree|freeze-evidence|final-wave-init|final-wave-audit)
		task=15
		if [ "$command" != final-wave-init ]; then
			task=$(value_after --task "$@")
		fi
		if [ "$command" != final-wave-init ] && [ "$task" != 15 ]; then
			fail "$command is installed only for task 15"
		fi
		exec "$script_dir/mem_leak_checker_task15_qa.sh" "$command" "$@"
		;;
	budget-negative)
		task=$(value_after --task "$@")
		[ "$task" = 10 ] || fail "budget-negative is installed only for task 10"
		exec "$script_dir/mem_leak_checker_task10_qa.sh" budget-negative "$@"
		;;
	compile-negative)
		task=$(value_after --task "$@")
		fixture=$(value_after --fixture "$@")
		[ "$task" = 2 ] || fail "compile-negative is installed only for task 2"
		[ "$fixture" = mlc_alloc_padding_unrepresentable ] || fail "unknown compile-negative fixture: $fixture"
		receipt_sha=$(task2_receipt verify "$fixture" "$0 $command $*")
		"$script_dir/mem_leak_checker_alloc_bounds_qa.sh" --fixtures "$fixture"
		printf 'MLC_QA_COMPILE_NEGATIVE task=2 fixture=%s red_linkage=verified receipt_sha256=%s\n' "$fixture" "$receipt_sha"
		;;
	receipt-negative)
		[ "$#" -ge 2 ] && [ "$1" = --task ] || usage_fail "receipt-negative requires leading --task"
		task=$(value_after --task "$@")
		if [ "$task" = 6 ]; then
			[ "$#" -eq 2 ] || usage_fail "unexpected task-6 receipt-negative options"
			exec "$script_dir/mem_leak_checker_task6_qa.sh" receipt-negative
		fi
		if [ "$task" = 8 ]; then
			[ "$#" -eq 2 ] || usage_fail "unexpected task-8 receipt-negative options"
			exec "$script_dir/mem_leak_checker_task8_qa.sh" negative
		fi
		if [ "$task" = 7 ]; then
			[ "$#" -eq 2 ] || usage_fail "unexpected task-7 receipt-negative options"
			exec "$script_dir/mem_leak_checker_task7_qa.sh" negative
		fi
		if [ "$task" = 5 ]; then
			[ "$#" -eq 2 ] || usage_fail "unexpected task-5 receipt-negative options"
			exec "$script_dir/mem_leak_checker_task5_scenarios.sh" receipt-negative
		fi
		if [ "$task" = 3 ]; then
			[ "$#" -eq 2 ] || usage_fail "unexpected receipt-negative options"
			exec python3 "$python_runner" task-3-receipt-negative
		fi
		[ "$task" = 2 ] || fail "receipt-negative is installed only for tasks 2 and 3"
		receipt_sha=$(task2_receipt negative "" "")
		printf 'MLC_QA_RECEIPT_NEGATIVE task=2 cases=symlink,wrong_branch,baseline_mismatch,cross_fixture,cross_command status=PASS receipt_sha256=%s\n' "$receipt_sha"
		;;
	seal-self-test)
		[ "$#" -eq 2 ] || usage_fail "unexpected seal-self-test options"
		task=$(value_after --task "$@")
		case "$task" in
			5) exec "$script_dir/mem_leak_checker_task5_seal_test.sh" ;;
			8) exec "$script_dir/mem_leak_checker_task8_seal_test.sh" ;;
			*) fail "seal-self-test is installed only for tasks 5 and 8" ;;
		esac
		;;
	evidence)
		[ "$#" -eq 2 ] || usage_fail "unexpected evidence options"
		task=$(value_after --task "$@")
		[ "$task" = 3 ] || fail "toolchain evidence is installed only for task 3"
		exec python3 "$python_runner" task-3-toolchain-evidence
		;;
	seal-task)
		[ "$#" -ge 4 ] || usage_fail "unexpected seal-task options"
		[ "$1" = --task ] && [ "$3" = --source ] || usage_fail "seal-task option order mismatch"
		task=$(value_after --task "$@")
		source=$(value_after --source "$@")
		if [ "$task" = 15 ]; then
			[ "$#" -eq 4 ] || {
				[ "$#" -eq 6 ] && [ "$5" = --output-dir ] && [ -n "$6" ] ||
					usage_fail "unexpected task-15 seal-task options"
			}
			if [ "$#" -eq 4 ]; then
				exec "$script_dir/mem_leak_checker_task15_qa.sh" seal-task --source "$source"
			fi
			exec "$script_dir/mem_leak_checker_task15_qa.sh" seal-task --source "$source" \
				--output-dir "$6"
		fi
		[ "$#" -eq 4 ] || usage_fail "unexpected seal-task options"
		if [ "$task" = 8 ]; then
			exec "$script_dir/mem_leak_checker_task8_qa.sh" seal-authoritative "$source"
		fi
		if [ "$task" = 9 ]; then
			exec "$script_dir/mem_leak_checker_task9_qa.sh" seal "$source"
		fi
		if [ "$task" = 10 ]; then
			exec "$script_dir/mem_leak_checker_task10_qa.sh" seal "$source"
		fi
		if [ "$task" = 11 ]; then
			[ -z "$(git -C "$source_root" status --porcelain --untracked-files=all)" ] || fail "seal requires clean receiving worktree"
			"$script_dir/mem_leak_checker_task11_qa.sh" host
			"$script_dir/mem_leak_checker_task11_qa.sh" static
			printf 'MLC_TASK11_SEAL status=PASS source=%s\n' "$(git -C "$source_root" rev-parse "$source")"
			exit 0
		fi
		if [ "$task" = 12 ]; then
			exec "$script_dir/mem_leak_checker_task12_qa.sh" seal "$source"
		fi
		if [ "$task" = 13 ]; then
			exec "$script_dir/mem_leak_checker_task13_qa.sh" seal "$source"
		fi
		if [ "$task" = 14 ]; then
			exec "$script_dir/mem_leak_checker_task14_qa.sh" seal "$source"
		fi
		if [ "$task" = 7 ]; then
			exec "$script_dir/mem_leak_checker_task7_qa.sh" seal "$source"
		fi
		if [ "$task" = 5 ]; then
			exec "$script_dir/mem_leak_checker_task5_scenarios.sh" seal \
				--source "$source"
		fi
		if [ "$task" = 6 ]; then
			exec "$script_dir/mem_leak_checker_task6_qa.sh" seal \
				--source "$source"
		fi
		if [ "$task" = 3 ]; then
			exec python3 "$python_runner" task-3-seal --source "$source"
		fi
		if [ "$task" = 4 ]; then
			exec "$script_dir/mem_leak_checker_task4_qa.sh" seal-authoritative \
				--source "$source"
		fi
		[ "$task" = 1 ] || fail "task scenario is not installed: $task"
		receiving_sha=$(git -C "$source_root" rev-parse "$source")
		[ "$receiving_sha" = "$(git -C "$source_root" rev-parse HEAD)" ] || fail "receiving SHA drift"
		[ -z "$(git -C "$source_root" status --porcelain --untracked-files=all)" ] || fail "seal requires clean receiving worktree"
		evidence_dir=$("$0" context --print-evidence-dir)
		seal_dir="$evidence_dir/task-1/task-1-post-integration"
		mkdir "$seal_dir"
		"$0" self-test --task 1 \
			--cases timeout,malformed,incomplete-with-verdict,missing-second-prompt,bad-red-linkage,pipeline-failure,missing-root,preflight-context-create-failure,preflight-context-fsync-failure,preflight-worktree-partial,preflight-evidence-dir-failure,preflight-identity-failure,preflight-concurrent-loser,preflight-ancestor-symlink,preflight-ancestor-replacement,preflight-existing-context-refusal,preflight-owned-child-cleanup \
			--output-json "$seal_dir/preflight-self-test.json"
		exec "$0" scenario-static --task 1 --output-json "$seal_dir/task-1-post-integration.json"
		;;
	*)
		fail "unknown command: $command"
		;;
esac
