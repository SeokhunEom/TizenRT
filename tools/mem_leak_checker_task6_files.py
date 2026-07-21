from __future__ import annotations

import os
from pathlib import Path, PurePosixPath
import stat

from mem_leak_checker_task6_schema import fail


def read_regular(root: Path, relative: str) -> bytes:
    candidate = PurePosixPath(relative)
    if candidate.is_absolute() or any(part in {"", ".", ".."} for part in candidate.parts):
        raise fail(relative, "safe relative path required")
    nofollow = getattr(os, "O_NOFOLLOW", 0)
    directory = os.open(root, os.O_RDONLY | os.O_DIRECTORY | nofollow)
    try:
        for component in candidate.parent.parts:
            try:
                child = os.open(
                    component, os.O_RDONLY | os.O_DIRECTORY | nofollow,
                    dir_fd=directory,
                )
            except OSError as error:
                raise fail(relative, "descriptor-bound directory required") from error
            os.close(directory)
            directory = child
        try:
            descriptor = os.open(
                candidate.name, os.O_RDONLY | os.O_NONBLOCK | nofollow,
                dir_fd=directory,
            )
        except OSError as error:
            raise fail(relative, "descriptor-bound regular file required") from error
        try:
            before = os.fstat(descriptor)
            if not stat.S_ISREG(before.st_mode):
                raise fail(relative, "regular file required")
            chunks: list[bytes] = []
            while chunk := os.read(descriptor, 65536):
                chunks.append(chunk)
            after = os.fstat(descriptor)
            named = os.stat(candidate.name, dir_fd=directory, follow_symlinks=False)
            identity = (before.st_dev, before.st_ino, before.st_size)
            if (
                identity != (after.st_dev, after.st_ino, after.st_size)
                or identity != (named.st_dev, named.st_ino, named.st_size)
            ):
                raise fail(relative, "descriptor identity or size changed")
            content = b"".join(chunks)
            if len(content) != before.st_size:
                raise fail(relative, "descriptor size changed during read")
            return content
        finally:
            os.close(descriptor)
    finally:
        os.close(directory)


def read_path(path: Path) -> bytes:
    return read_regular(path.parent.resolve(), path.name)
