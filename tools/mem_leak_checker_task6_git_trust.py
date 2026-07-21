from __future__ import annotations

from dataclasses import dataclass
import os
from pathlib import Path
import subprocess

from mem_leak_checker_task6_schema import fail

GIT = "/usr/bin/git"
TRUSTED_PATH = "/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin"


@dataclass(frozen=True, slots=True)
class GitAdminSnapshot:
    effective_config: bytes
    replace_refs: bytes
    index_flags: bytes


def git_environment() -> dict[str, str]:
    return {
        "GIT_CONFIG_GLOBAL": "/dev/null",
        "GIT_CONFIG_NOSYSTEM": "1",
        "GIT_NO_REPLACE_OBJECTS": "1",
        "LC_ALL": "C",
        "PATH": TRUSTED_PATH,
        "TMPDIR": "/tmp",
    }


def run(root: Path, *arguments: str, check: bool = True) -> subprocess.CompletedProcess[bytes]:
    result = subprocess.run(
        [GIT, "-C", str(root), *arguments], check=False,
        capture_output=True, env=git_environment(),
    )
    if check and result.returncode != 0:
        raise fail("git", result.stderr.decode(errors="replace").strip())
    return result


def capture(root: Path) -> GitAdminSnapshot:
    return GitAdminSnapshot(
        effective_config=run(
            root, "config", "--null", "--show-origin", "--show-scope", "--list",
        ).stdout,
        replace_refs=run(root, "for-each-ref", "--format=%(refname)%00%(objectname)", "refs/replace").stdout,
        index_flags=run(root, "ls-files", "-v").stdout,
    )


def validate(root: Path) -> GitAdminSnapshot:
    snapshot = capture(root)
    if snapshot.replace_refs:
        raise fail("git.replace", "receiving replace refs rejected")
    unsafe_exact = {
        "core.fsmonitor", "core.fsmonitorhookversion", "core.hookspath",
        "core.worktree", "core.sparsecheckout", "core.sparsecheckoutcone",
        "core.attributesfile", "core.excludesfile", "diff.external",
    }
    names = tuple(
        name.lower()
        for name in run(root, "config", "--name-only", "--list").stdout.decode().splitlines()
    )
    if any(name in unsafe_exact or (name.startswith("filter.") and name.rsplit(".", 1)[-1] in {"clean", "smudge", "process"}) for name in names):
        raise fail("git.config", "behavior-changing local Git config rejected")
    if any(record[:1] == b"S" or record[:1].islower() for record in snapshot.index_flags.splitlines()):
        raise fail("git.index", "assume-unchanged or skip-worktree flags rejected")
    return snapshot


def sanitized_process_environment() -> dict[str, str]:
    environment = dict(os.environ)
    prefixes = (
        "GIT_", "PYTHON", "DYLD_", "LD_",
    )
    names = {
        "BASH_ENV", "ENV", "CPATH", "C_INCLUDE_PATH", "CPLUS_INCLUDE_PATH",
        "LIBRARY_PATH", "SDKROOT", "CC", "CXX", "CPP", "CFLAGS",
        "CXXFLAGS", "CPPFLAGS", "LDFLAGS", "AR", "AS", "NM", "OBJCOPY",
        "OBJDUMP", "RANLIB", "STRIP", "COMPILER_PATH", "GCC_EXEC_PREFIX",
        "DEVELOPER_DIR", "MACOSX_DEPLOYMENT_TARGET", "PKG_CONFIG_PATH",
        "PKG_CONFIG_LIBDIR",
    }
    for name in tuple(environment):
        if name in names or name.startswith(prefixes):
            environment.pop(name)
    environment.update(git_environment())
    environment["PYTHONDONTWRITEBYTECODE"] = "1"
    for name in ("MLC_TASK6_CONTEXT_FIXTURE", "MLC_TASK6_EVIDENCE_DIR"):
        value = os.environ.get(name)
        if value is not None:
            environment[name] = value
    return environment
