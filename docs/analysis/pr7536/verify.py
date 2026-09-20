#!/usr/bin/env python3
"""Run PR #7536 source-extraction host regressions; no board or flash access."""

import argparse
import json
import re
import shutil
import subprocess
import tempfile
from pathlib import Path

HEAD = "c7a98ec37fb0240a67cd99e8b09ca2fc775db33d"
BASE = "df09bb8a19eed8e63b66a5b91ba349aefd133b20"
HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
INTERNAL = "os/kernel/binary_manager/binary_manager_internal.h"
PUBLIC = "os/include/tinyara/binary_manager.h"
BOOT = "os/kernel/binary_manager/binary_manager_bootparam.c"
LOAD = "os/kernel/binary_manager/binary_manager_load.c"
PATCH = HERE / "0001-preserve-bp-reason-values-and-internal-api.patch"


def run(args, **kwargs):
    completed = subprocess.run(args, text=True, capture_output=True, **kwargs)
    if completed.returncode:
        raise RuntimeError(f"Command failed: {args}\n{completed.stdout}{completed.stderr}")
    return completed


def function(source, name):
    """Extract an unchanged real function, retaining its preprocessor branches."""
    match = re.search(r"^(?:static )?[^\n;{}]+\b" + name + r"\([^;]*?\)\n\{", source, re.M)
    if not match:
        raise ValueError("Function not found: " + name)
    # Blank comments and literals while keeping offsets stable for brace matching.
    masked = re.sub(r'/\*[\s\S]*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
                    lambda m: " " * len(m.group()), source)
    start = source.index("{", match.start())
    depth = 0
    for i in range(start, len(source)):
        depth += (masked[i] == "{") - (masked[i] == "}")
        if depth == 0:
            return source[match.start():i + 1] + "\n"
    raise ValueError("Unclosed function: " + name)


def generate(root):
    header = (root / INTERNAL).read_text()
    boot = (root / BOOT).read_text()
    load = (root / LOAD).read_text()
    types = header[header.index("/* User bootparam data */"):
                   header.index("typedef struct binmgr_bp_recovery_info_s binmgr_bp_recovery_info_t;")
                   + len("typedef struct binmgr_bp_recovery_info_s binmgr_bp_recovery_info_t;")]
    boot_functions = "\n".join(function(boot, name) for name in (
        "binary_manager_open_bootparam",
        "binary_manager_write_bootparam_to_slot",
        "binary_manager_is_bp_kernel_address_valid",
        "binary_manager_is_set_mismatch",
        "binary_manager_make_bootparam_from_partitions",
        "binary_manager_check_bootparam_set",
        "binary_manager_set_bp_recovery_reason",
    ))
    source = (HERE / "host_harness.c.in").read_text()
    return source.replace("/* INSERT_REAL_TYPES */", types).replace(
        "/* INSERT_REAL_BOOT_FUNCTIONS */", boot_functions).replace(
        "/* INSERT_REAL_LOAD_FUNCTION */", function(load, "binary_manager_load"))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, help="Write JSON evidence outside temporary build")
    args = parser.parse_args()
    compiler = shutil.which("clang")
    if not compiler:
        raise SystemExit("clang is required")
    run(["git", "cat-file", "-e", HEAD + "^{commit}"], cwd=REPO)
    result = {"head": HEAD, "merge_base": BASE,
              "compiler": run([compiler, "--version"]).stdout.splitlines()[0],
              "scope": "Extracted real C functions; mocked I/O, CRC and runtime dependencies; no target build",
              "cases": []}
    with tempfile.TemporaryDirectory(prefix="pr7536-host-") as directory:
        work = Path(directory)
        original = work / "original"
        for path in (INTERNAL, PUBLIC, BOOT, LOAD):
            target = original / path
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(run(["git", "show", HEAD + ":" + path], cwd=REPO).stdout)
        patched = work / "patched"
        shutil.copytree(original, patched)
        # A disposable repository prevents git apply from discovering the caller's repo.
        run(["git", "init", "-q"], cwd=patched)
        run(["git", "apply", "--check", str(PATCH)], cwd=patched)
        run(["git", "apply", str(PATCH)], cwd=patched)
        result["patch_apply_check"] = "passed"
        assert "void binary_manager_set_bp_recovery_reason" not in (patched / PUBLIC).read_text()
        assert "void binary_manager_set_bp_recovery_reason" in (patched / INTERNAL).read_text()
        # BP off plus four app combinations and a kernel/resource-only configuration.
        configs = [
            ("bp-off", []),
            ("kernel", ["CONFIG_USE_BP"]),
            ("kernel-resource", ["CONFIG_USE_BP", "CONFIG_RESOURCE_FS"]),
            ("app", ["CONFIG_USE_BP", "CONFIG_APP_BINARY_SEPARATION"]),
            ("app-sign", ["CONFIG_USE_BP", "CONFIG_APP_BINARY_SEPARATION", "CONFIG_BINARY_SIGNING"]),
            ("common", ["CONFIG_USE_BP", "CONFIG_APP_BINARY_SEPARATION", "CONFIG_SUPPORT_COMMON_BINARY"]),
            ("common-resource-sign", ["CONFIG_USE_BP", "CONFIG_APP_BINARY_SEPARATION",
                                      "CONFIG_SUPPORT_COMMON_BINARY", "CONFIG_RESOURCE_FS", "CONFIG_BINARY_SIGNING"]),
        ]
        for label, root in (("original", original), ("patched", patched)):
            src = work / (label + ".c")
            src.write_text(generate(root))
            for config, macros in configs:
                exe = work / (label + "-" + config)
                run([compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                     "-Wno-unused-function", "-Wno-pointer-bool-conversion", "-Wno-sign-compare",
                     "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-g",
                     *["-D" + macro for macro in macros], str(src), "-o", str(exe)])
                completed = run([str(exe)])
                result["cases"].append({"source": label, "config": config,
                                        "result": completed.stdout.strip()})
                abi = subprocess.run([str(exe), "--legacy-abi"], text=True, capture_output=True)
                expected = 1 if label == "original" else 0
                if abi.returncode != expected:
                    raise RuntimeError(f"Unexpected ABI regression result: {label} {config}: {abi.stderr}")
                result["cases"].append({"source": label, "config": config + "/legacy-abi",
                                        "exit_code": abi.returncode, "result": abi.stdout.strip()})
    result["result"] = "passed: original legacy collision reproduced; patched compatibility and behavior checks passed"
    rendered = json.dumps(result, ensure_ascii=False, indent=2) + "\n"
    if args.output:
        args.output.write_text(rendered)
    print(rendered, end="")


if __name__ == "__main__":
    main()
