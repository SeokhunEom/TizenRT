from __future__ import annotations

import fcntl
import os
from pathlib import Path
import stat

from mem_leak_checker_task6_schema import fail
from mem_leak_checker_task6_types import ContractError


def _fault(path: Path, name: str) -> bool:
    configured = os.environ.get("MLC_TASK6_PUBLISH_FAULT")
    private = str(path).startswith(("/tmp/mlc-task6-", "/private/tmp/mlc-task6-"))
    if configured is not None and not private:
        raise fail("publication", "fault injection forbidden outside private test root")
    return configured == name


def _remove_owned(directory: int, name: str, identity: tuple[int, int]) -> None:
    try:
        current = os.stat(name, dir_fd=directory, follow_symlinks=False)
    except FileNotFoundError:
        return
    if (current.st_dev, current.st_ino) == identity:
        os.unlink(name, dir_fd=directory)
        os.fsync(directory)


def publish(path: Path, encoded: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    nofollow = getattr(os, "O_NOFOLLOW", 0)
    directory = os.open(path.parent, os.O_RDONLY | os.O_DIRECTORY | nofollow)
    created: tuple[int, int] | None = None
    try:
        fcntl.flock(directory, fcntl.LOCK_EX)
        try:
            descriptor = os.open(
                path.name, os.O_WRONLY | os.O_CREAT | os.O_EXCL | nofollow,
                0o600, dir_fd=directory,
            )
        except FileExistsError:
            descriptor = os.open(
                path.name, os.O_RDONLY | os.O_NONBLOCK | nofollow,
                dir_fd=directory,
            )
            try:
                if not stat.S_ISREG(os.fstat(descriptor).st_mode):
                    raise fail("receipt", "regular replay target required")
                if os.read(descriptor, len(encoded) + 1) != encoded:
                    raise fail("receipt", "idempotent replay mismatch")
                os.fsync(descriptor)
            finally:
                os.close(descriptor)
        else:
            owned = os.fstat(descriptor)
            created = (owned.st_dev, owned.st_ino)
            try:
                written = 0
                while written < len(encoded):
                    pending = encoded[written:]
                    if _fault(path, "partial-write"):
                        pending = pending[:max(1, len(pending) // 2)]
                    count = os.write(descriptor, pending)
                    if count <= 0:
                        raise OSError("short receipt write")
                    written += count
                    if _fault(path, "partial-write"):
                        raise OSError("injected partial receipt write")
                if _fault(path, "file-fsync"):
                    raise OSError("injected receipt fsync failure")
                os.fsync(descriptor)
            except (OSError, ContractError):
                _remove_owned(directory, path.name, created)
                created = None
                raise
            finally:
                os.close(descriptor)
        if _fault(path, "directory-fsync"):
            raise OSError("injected directory fsync failure")
        os.fsync(directory)
    except (OSError, ContractError):
        if created is not None:
            _remove_owned(directory, path.name, created)
        raise
    finally:
        os.close(directory)
