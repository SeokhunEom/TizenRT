#!/usr/bin/env python3
"""Verify that a failed Docker build cannot be hidden by log tee pipelines."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
FAKE_DOCKER = """#!/usr/bin/env bash
printf '%s\\n' "$*" >> "$DOCKER_CALLS"
case "$1" in
    info) echo arm64 ;;
    build)
        echo 'image build log'
        if [ "$FAIL_AT" = image ]; then exit 23; fi ;;
    image) echo sha256:fake ;;
    run)
        case "$*" in
            *--name*) echo 'firmware build failed'; exit 29 ;;
            *) echo 'tool versions' ;;
        esac ;;
    rm) exit 0 ;;
    *) exit 99 ;;
esac
"""


class BuildFailureTests(unittest.TestCase):
    def test_docker_failures_keep_exit_status_logs_and_cleanup(self):
        for failure, expected, log in [('image', 23, 'image-build.log'),
                                       ('firmware', 29, 'build.log')]:
            with self.subTest(failure=failure), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                docker = root / 'docker'
                docker.write_text(FAKE_DOCKER)
                docker.chmod(0o755)
                out = root / 'evidence'
                calls = root / 'docker-calls'
                env = dict(os.environ, PATH=str(root) + os.pathsep + os.environ['PATH'],
                           FAIL_AT=failure, DOCKER_CALLS=str(calls))
                process = subprocess.run(['bash', str(HERE / 'ci-build.sh'), str(out)], env=env,
                                         stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                self.assertEqual(process.returncode, expected, process.stderr.decode())
                self.assertTrue((out / log).read_text())
                self.assertFalse((out / 'build.json').exists())
                self.assertIn('rm -f qemu-build-test-ci-', calls.read_text())


if __name__ == '__main__':
    unittest.main()
