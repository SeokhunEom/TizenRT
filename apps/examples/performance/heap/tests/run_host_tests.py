#!/usr/bin/env python3
"""Functional/fault tests; host scheduling is simulated, never a performance result."""
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
SOURCE = HERE.parent
ROOT = SOURCE.parents[3]


def main():
    clang = os.environ.get("CC", "clang")
    count = 0
    host_flags = shlex.split(os.environ.get("HEAP_HOST_CFLAGS", ""))
    with tempfile.TemporaryDirectory(prefix="tizenrt-heap-test-") as directory:
        tmp = Path(directory)
        (tmp / "tinyara").mkdir()
        (tmp / "tinyara/config.h").write_text("#define CONFIG_CLOCK_MONOTONIC 1\n")
        binaries = {}
        for name, defines in {
            "up": ["-DCONFIG_RR_INTERVAL=10"],
            "smp": ["-DCONFIG_RR_INTERVAL=10", "-DCONFIG_SMP=1", "-DCONFIG_SMP_NCPUS=2"],
            "fifo": ["-DCONFIG_RR_INTERVAL=0"],
        }.items():
            binary = tmp / name
            subprocess.run([clang, *host_flags, "-std=c99", "-Wall", "-Wextra", "-Werror", *defines,
                            "-I" + str(tmp), "-I" + str(SOURCE), str(HERE / "heap_host.c"),
                            "-pthread", "-o", str(binary)], check=True)
            binaries[name] = binary

        def run(name, args, success=True, env=None, allocations=None, contains=()):
            nonlocal count
            result = subprocess.run([str(binaries[name]), *args], text=True, capture_output=True,
                                    timeout=20, env={**os.environ, **(env or {})})
            assert (result.returncode == 0) == success, (args, env, result.stdout, result.stderr)
            assert "live=0" in result.stdout, result.stdout
            found = re.search(r"created=(\d+) joined=(\d+)", result.stdout)
            assert found and found[1] == found[2], result.stdout
            if allocations is not None:
                assert f"malloc_ok={allocations} frees={allocations}" in result.stdout, result.stdout
            for text in contains:
                assert text in result.stdout, (text, result.stdout)
            if not success:
                assert "Heap performance FAIL" in result.stdout and "Heap performance PASS" not in result.stdout
            count += 1

        short = ["--blocks", "3", "--repeat", "2", "--samples", "2", "--warmup", "1",
                 "--min-size", "16", "--max-size", "16", "--interval", "0"]
        run("up", ["selftest"], allocations=6)
        run("up", ["--help"], allocations=0, contains=["INTERVAL_SECONDS", "--mode"])
        run("up", short, allocations=18, contains=["malloc=6 free=6", "Heap performance PASS"])
        run("up", ["0", "1"], allocations=6000, contains=["Size=8192 bytes"])
        for mode in ["single", "samecpu", "smp", "samecpu-prio", "smp-prio"]:
            n = 1 if mode == "single" else 4
            for policy in ["fifo", "rr"]:
                run("smp", [*short, "--mode", mode, "--workers", str(n), "--policy", policy],
                    allocations=18 * n, contains=["aggregate makespan", "launch delay"])
        run("smp", [*short, "--mode", "smp-prio", "--workers", "4"],
            contains=["Worker=0 cpu=0 priority=100", "Worker=3 cpu=1 priority=130"])
        run("fifo", short, allocations=18, contains=["policy=fifo"])
        run("fifo", [*short, "--policy", "rr"], success=False)
        run("up", [*short, "--mode", "smp"], success=False)
        run("up", [*short, "--mode", "smp-prio"], success=False)
        for args in [
            ["--unknown", "1"], ["--repeat"], ["--repeat", "0"], ["--repeat", "-1"],
            ["--repeat", "12junk"], ["--repeat", " 12"], ["--repeat", "999999999999999999999"],
            ["--interval", "3601"], ["--samples", "32"], ["--blocks", "257"],
            ["--warmup", "11"], ["--mode", "invalid"], ["--mode", "single", "--workers", "2"],
            ["--mode", "samecpu", "--workers", "1"], ["--workers", "9"],
            ["--min-size", "17"], ["--min-size", "64", "--max-size", "32"],
            ["--mode", "samecpu-prio", "--workers", "8", "--priority", "250"],
            ["--priority", "256"], ["--policy", "other"], ["0", "0"], ["--help", "x"],
        ]:
            run("up", args, success=False, allocations=0)
        run("smp", [*short, "--mode", "smp-prio", "--workers", "8", "--max-size", "64",
                    "--samples", "31", "--warmup", "2"], allocations=4752,
            contains=["Size=16 bytes", "Size=32 bytes", "Size=64 bytes"])
        run("up", [*short, "--warmup", "0", "--samples", "1"], allocations=6)
        faults = {"FAIL_MALLOC": [1, 2, 7, 30], "FAIL_CALLOC": [1, 2, 3],
                  "FAIL_CLOCK": [1, 2, 3, 4, 7], "FAIL_CREATE": [1, 2, 4],
                  "FAIL_ATTR": [1, 5, 9], "FAIL_SEM": [1, 2, 3, 4, 6]}
        for fault, points in faults.items():
            for point in points:
                run("smp", [*short, "--mode", "smp", "--workers", "4"],
                    success=False, env={fault: str(point)})
        # Check allocator calls are not optimized away in an optimized build.
        subprocess.run([clang, *host_flags, "-O2", "-std=c99", "-Wall", "-Wextra", "-Werror",
                        "-DCONFIG_RR_INTERVAL=10", "-I" + str(tmp), "-I" + str(SOURCE),
                        str(HERE / "heap_host.c"), "-pthread", "-o", str(binaries["up"])], check=True)
        run("up", short, allocations=18)
        print(f"PASS: {count} host functional/fault scenarios (not target timing evidence)")

        # Compile the unchanged production source against actual target headers.
        overlay = tmp / "target"
        (overlay / "tinyara").mkdir(parents=True)
        shutil.copytree(ROOT / "os/arch/arm/include", overlay / "arch", symlinks=True)
        (overlay / "arch/chip").symlink_to("amebasmart", target_is_directory=True)
        for name, recipe, up in [
            ("flat-smp", "flat_apps", False),
            ("flat-up-fifo", "flat_apps", True),
            ("protected-smp", "loadable_ext_ddr", False),
        ]:
            opts = {}
            for line in (ROOT / f"build/configs/rtl8730e/{recipe}/defconfig").read_text().splitlines():
                if line.startswith("CONFIG_") and "=" in line:
                    k, v = line.split("=", 1)
                    opts[k] = v
                elif line.startswith("# CONFIG_") and line.endswith(" is not set"):
                    opts.pop(line.split()[1], None)
            opts.update(CONFIG_CLOCK_MONOTONIC="y", CONFIG_LIBC_LONG_LONG="y")
            if up:
                opts.pop("CONFIG_SMP", None)
                opts["CONFIG_RR_INTERVAL"] = "0"
            (overlay / "tinyara/config.h").write_text("\n".join(
                f"#define {k} {'1' if v == 'y' else v}" for k, v in opts.items()) + "\n")
            subprocess.run([clang, "--target=arm-none-eabi", "-mcpu=cortex-a32", "-mthumb",
                            "-O2", "-std=gnu99", "-ffreestanding", "-Wall", "-Wextra", "-Werror",
                            # Existing net.h forward declarations trigger this unrelated warning.
                            "-Wno-visibility", "-I" + str(overlay), "-I" + str(ROOT / "os/include"),
                            "-c", str(SOURCE / "heap_performance_test.c"),
                            "-o", str(tmp / (name + ".o"))], check=True)
            print(f"PASS: ARM object compilation, {name} (no target link/boot)")


if __name__ == "__main__":
    main()
