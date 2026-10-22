#!/usr/bin/env python3
"""Verify pinned NuttX files and explicitly recorded integration changes."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--upstream', type=Path,
                        help='optional NuttX Git checkout for provenance verification')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    manifest = json.loads((root / 'tools/nuttx-sync/modules/mm.json').read_text())
    deviations = manifest.get('deviations', {})
    seen = set()
    failures = []
    unchanged = 0
    changed = 0
    for entry in manifest['files']:
        name = entry['path']
        target = (root / name).resolve()
        try:
            target.relative_to(root)
            contained = True
        except ValueError:
            contained = False
        if not contained or name in seen:
            failures.append(f'invalid or duplicate destination: {name}')
            continue
        seen.add(name)
        if args.upstream:
            try:
                original = subprocess.check_output(
                    ['git', '-C', str(args.upstream), 'show',
                     manifest['commit'] + ':' + entry['upstream']],
                    stderr=subprocess.PIPE)
                if hashlib.sha256(original).hexdigest() != entry['sha256']:
                    failures.append(f'upstream hash mismatch: {name}')
            except subprocess.CalledProcessError:
                failures.append(f'upstream object unavailable: {name}')
        if not target.is_file():
            failures.append(f'missing imported file: {name}')
            continue
        actual = hashlib.sha256(target.read_bytes()).hexdigest()
        if name in deviations:
            change = deviations[name]
            if not change.get('reason') or not change.get('sha256'):
                failures.append(f'incomplete deviation: {name}')
            elif actual != change['sha256']:
                failures.append(f'unrecorded change to deviation: {name}')
            elif actual == entry['sha256']:
                failures.append(f'obsolete deviation: {name}')
            else:
                changed += 1
        elif actual != entry['sha256']:
            failures.append(f'unrecorded upstream difference: {name}')
        else:
            unchanged += 1
    for name in deviations.keys() - seen:
        failures.append(f'deviation is not an imported file: {name}')
    result = {'tag': manifest['tag'], 'commit': manifest['commit'],
              'unchanged': unchanged, 'documented_changes': changed,
              'failures': failures}
    print(json.dumps(result, indent=2))
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
