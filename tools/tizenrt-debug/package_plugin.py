#!/usr/bin/env python3
"""Validate local components and create a reproducible private plugin archive."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import zipfile

REPO = Path(__file__).resolve().parents[2]
PLUGIN = REPO / "plugins/tizenrt-debug-analyzer"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    manifest = json.loads((PLUGIN / "plugin.json").read_text())
    compatibility = json.loads((PLUGIN / ".codex-plugin/plugin.json").read_text())
    mcp = json.loads((PLUGIN / "mcp.json").read_text())
    assert manifest["$schema"] == "https://agent-plugins.org/schemas/1.0.0/plugin.schema.json"
    assert mcp["$schema"] == "https://agent-plugins.org/schemas/1.0.0/mcp.schema.json"
    assert manifest["name"] == PLUGIN.name == compatibility["name"]
    assert re.fullmatch(r"\d+\.\d+\.\d+", manifest["version"])
    assert manifest["version"] == compatibility["version"]
    assert manifest["extensions"]["com.openai"]["interface"] == compatibility["interface"]
    assert len(compatibility["interface"]["shortDescription"]) <= 30
    assert (PLUGIN / "server.py").is_file()
    files = []
    for path in sorted(PLUGIN.rglob("*")):
        relative = path.relative_to(PLUGIN)
        if any(part in {"__pycache__", ".venv", "dist"} for part in relative.parts):
            continue
        assert not path.is_symlink(), "package symlink: %s" % relative
        if not path.is_file() or path.suffix == ".pyc":
            continue
        if path.suffix == ".json":
            json.loads(path.read_text(encoding="utf-8"))
        files.append((path, relative))
    skills = list(PLUGIN.glob("skills/*/SKILL.md"))
    assert skills
    for path in skills:
        content = path.read_text(encoding="utf-8")
        assert content.startswith("---\n") and "\n---\n" in content[4:]
        frontmatter = content.split("---", 2)[1]
        assert "name: " in frontmatter and "description: " in frontmatter
        for target in re.findall(r"\]\(([^)]+)\)", content):
            if "://" not in target:
                resolved = (path.parent / target).resolve()
                assert resolved.is_relative_to(PLUGIN.resolve()) and resolved.is_file(), target
    output = (args.output or REPO / "build/tizenrt-debug" / (manifest["name"] + "-" + manifest["version"] + ".zip")).resolve()
    assert not output.is_relative_to(PLUGIN.resolve()), "archive must be outside plugin directory"
    output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for path, relative in files:
            info = zipfile.ZipInfo((Path(PLUGIN.name) / relative).as_posix(), (2026, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
            archive.writestr(info, path.read_bytes())
    with zipfile.ZipFile(output) as archive:
        assert archive.testzip() is None
        assert set(archive.namelist()) == {(Path(PLUGIN.name) / rel).as_posix() for _, rel in files}
        for name in ("plugin.json", "mcp.json", ".mcp.json", ".codex-plugin/plugin.json"):
            json.loads(archive.read(PLUGIN.name + "/" + name))
    print(json.dumps({"archive": str(output), "files": len(files), "sha256": hashlib.sha256(output.read_bytes()).hexdigest()}))


if __name__ == "__main__":
    main()
