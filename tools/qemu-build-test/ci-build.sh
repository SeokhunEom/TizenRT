#!/usr/bin/env bash
# Prepare the CI image and clean-build firmware; also usable outside Actions.
set -euo pipefail

root="$(cd "$(dirname "$0")/../.." && pwd)"
output="${1:?Usage: bash tools/qemu-build-test/ci-build.sh NEW_OUTPUT_DIRECTORY}"
mkdir -p "$output"
output="$(cd "$output" && pwd)"
if [ -n "$(ls -A "$output")" ]; then
    echo "Output directory must be new or empty: $output" >&2
    exit 2
fi
image="tizenrt/qemu-build-test-ci:2.12.0-gcc10.3"
builder="qemu-build-test-ci-$$"
context="$(mktemp -d)"
cleanup() {
    status=$?
    trap - EXIT
    docker rm -f "$builder" >/dev/null 2>&1 || true
    rm -rf "$context"
    exit "$status"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

architecture="$(docker info --format '{{.Architecture}}')"
case "$architecture" in
    arm64|aarch64) ;;
    *) echo "A native ARM64 Docker daemon is required, got: $architecture" >&2; exit 2 ;;
esac
cp "$root/build/configs/qemu/qemu-2.12.0-rc1_16m_ram_size.patch" "$context/16m.patch"
docker build --platform linux/arm64 --file "$root/tools/qemu-build-test/Dockerfile.ci" \
    --tag "$image" "$context" 2>&1 | tee "$output/image-build.log"
image_id="$(docker image inspect --format '{{.Id}}' "$image")"
printf '%s\n' "$image_id" > "$output/image-id.txt"
docker run --rm --pull=never --platform linux/arm64 --network none "$image_id" \
    bash -euc 'arm-none-eabi-gcc --version; arm-none-eabi-ld --version; qemu-system-arm --version; python3 --version' \
    2>&1 | tee "$output/versions.log"

docker run --rm --pull=never --platform linux/arm64 --network none --name "$builder" \
    -v "$root:/work" -w /work/os "$image_id" bash -euc '
        make distclean
        cd tools
        ./configure.sh qemu/build_test
        cd ..
        make -j4
        cmp .config ../build/configs/qemu/build_test/defconfig
        test -s ../build/output/bin/tinyara
        test -s ../build/output/bin/tinyara.bin
        arm-none-eabi-size ../build/output/bin/tinyara
    ' 2>&1 | tee "$output/build.log"
mkdir -p "$output/firmware"
cp "$root/build/output/bin/tinyara" "$root/build/output/bin/tinyara.bin" "$output/firmware/"
cp "$root/os/.config" "$output/effective.config"
python3 - "$root" "$output" "$image_id" <<'PYMETA'
import hashlib, json, pathlib, subprocess, sys
root, out = map(pathlib.Path, sys.argv[1:3])
files = ['firmware/tinyara', 'firmware/tinyara.bin', 'effective.config']
record = {
    'status': 'pass',
    'image_id': sys.argv[3],
    'source_commit': subprocess.check_output(['git', '-C', str(root), 'rev-parse', 'HEAD']).decode().strip(),
    'source_status': subprocess.check_output(['git', '-C', str(root), 'status', '--porcelain']).decode(),
    'sha256': {name: hashlib.sha256((out / name).read_bytes()).hexdigest() for name in files},
}
(out / 'build.json').write_text(json.dumps(record, indent=2) + '\n')
PYMETA
