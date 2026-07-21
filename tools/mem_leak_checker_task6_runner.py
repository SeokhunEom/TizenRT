#!/usr/bin/env python3
from __future__ import annotations

import os
import signal
import subprocess
import sys
from types import FrameType


def main() -> int:
    if len(sys.argv) < 2:
        return 64
    child = subprocess.Popen(sys.argv[1:], start_new_session=True)
    interrupted = 0

    def forward(signum: int, _frame: FrameType | None) -> None:
        nonlocal interrupted
        interrupted = signum
        try:
            os.killpg(child.pid, signum)
        except ProcessLookupError:
            pass

    for signum in (signal.SIGHUP, signal.SIGINT, signal.SIGTERM):
        signal.signal(signum, forward)
    ready = os.environ.get("MLC_TASK6_SIGNAL_READY_TEMP")
    if ready is not None:
        if not ready.startswith(("/tmp/mlc-task6.", "/private/tmp/mlc-task6.")):
            os.killpg(child.pid, signal.SIGTERM)
            child.wait()
            return 64
        print(f"MLC_TASK6_SIGNAL_READY temp={ready}", flush=True)
    result = child.wait()
    if interrupted:
        return 128 + interrupted
    return result if result >= 0 else 128 - result


if __name__ == "__main__":
    raise SystemExit(main())
