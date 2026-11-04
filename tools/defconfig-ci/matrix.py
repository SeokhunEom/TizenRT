#!/usr/bin/env python3
"""Discover every supported board's defconfig without evaluating shell code."""

import json
import os
from pathlib import Path
import re

# Start the portable/legacy configurations early for faster failure feedback.
BOARDS = ("qemu", "rtl8721csm", "bk7239n", "rtl8730e")
DEFAULT_DOCKER_VERSION = "1.5.8"  # os/dbuild.sh default


def discover(config_root):
    entries = []
    for board in BOARDS:
        configs = sorted((config_root / board).rglob("defconfig"))
        if not configs:
            raise ValueError(f"No defconfigs found for {board}")
        for path in configs:
            config = path.parent.relative_to(config_root).as_posix()
            # configure.sh expects exactly <board>/<configuration>.
            if not re.fullmatch(r"[a-z0-9_-]+/[a-zA-Z0-9_-]+", config):
                raise ValueError(f"Unsupported configuration path: {config}")
            versions = re.findall(r"^CONFIG_DOCKER_VERSION=(.*)$", path.read_text(), re.M)
            if len(versions) > 1:
                raise ValueError(f"Duplicate CONFIG_DOCKER_VERSION: {config}")
            version = DEFAULT_DOCKER_VERSION
            if versions:
                match = re.fullmatch(r'"([a-zA-Z0-9_][a-zA-Z0-9_.-]*)"', versions[0])
                if not match:
                    raise ValueError(f"Invalid CONFIG_DOCKER_VERSION: {config}")
                version = match.group(1)
            entries.append({
                "config": config,
                "id": config.replace("/", "--"),
                "image": f"tizenrt/tizenrt:{version}",
            })
    if len(entries) > 256:
        raise ValueError("GitHub Actions supports at most 256 jobs per matrix")
    if len({entry["id"] for entry in entries}) != len(entries):
        raise ValueError("Artifact names must be unique")
    return {"include": entries}


def main():
    root = Path(__file__).resolve().parents[2]
    matrix = discover(root / "build/configs")
    payload = json.dumps(matrix, separators=(",", ":"))
    print(payload)
    if os.environ.get("GITHUB_OUTPUT"):
        with open(os.environ["GITHUB_OUTPUT"], "a") as output:
            output.write(f"matrix={payload}\n")
    if os.environ.get("GITHUB_STEP_SUMMARY"):
        with open(os.environ["GITHUB_STEP_SUMMARY"], "a") as summary:
            summary.write(f"Building all {len(matrix['include'])} defconfigs.\n\n")
            summary.write("| Configuration | Docker image |\n| --- | --- |\n")
            for entry in matrix["include"]:
                summary.write(f"| {entry['config']} | {entry['image']} |\n")


if __name__ == "__main__":
    main()
