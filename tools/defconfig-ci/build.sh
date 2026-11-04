#!/usr/bin/env bash
# Build one defconfig in a fresh checkout with its declared Docker toolchain.
set -euo pipefail

root="$(cd "$(dirname "$0")/../.." && pwd)"
config="${1:?Usage: build.sh BOARD/CONFIG tizenrt/tizenrt:VERSION}"
image="${2:?Missing Docker image}"
if [[ ! "$config" =~ ^(rtl8730e|bk7239n|rtl8721csm|qemu)/[a-zA-Z0-9_-]+$ ]] ||
   [[ ! "$image" =~ ^tizenrt/tizenrt:[a-zA-Z0-9_][a-zA-Z0-9_.-]*$ ]] ||
   [[ ! -f "$root/build/configs/$config/defconfig" ]]; then
    echo "Invalid configuration or image: $config $image" >&2
    exit 2
fi
if [[ -e "$root/os/.config" ]] ||
   [[ -d "$root/build/output/bin" && -n "$(find "$root/build/output/bin" -mindepth 1 ! -name .gitignore -print -quit)" ]]; then
    echo "Use a fresh checkout; refusing to reuse existing build outputs" >&2
    exit 2
fi

output="$root/tmp/defconfig-ci"
mkdir -p "$output"
builder="defconfig-ci-${GITHUB_RUN_ID:-local}-${GITHUB_JOB:-build}-$$"
finish() {
    status=$?
    trap - EXIT
    docker rm -f "$builder" >/dev/null 2>&1 || true
    printf '%s\n' "$status" > "$output/exit-code.txt"
    if [[ -f "$root/os/.config" ]]; then
        cp "$root/os/.config" "$output/effective.config" || true
    fi
    if [[ -d "$root/build/output/bin" ]]; then
        # Archive partial output on failure as well as complete firmware on success.
        tar -czf "$output/firmware.tar.gz" -C "$root/build/output" bin || true
    fi
    if [[ -n "${GITHUB_STEP_SUMMARY:-}" ]]; then
        printf '### %s\n\nImage: `%s`\n\nBuild exit code: `%s`\n' \
            "$config" "$image" "$status" >> "$GITHUB_STEP_SUMMARY"
    fi
    exit "$status"
}
trap finish EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

git -C "$root" rev-parse HEAD > "$output/source-commit.txt"
printf '%s\n' "$config" > "$output/configuration.txt"
printf '%s\n' "$image" > "$output/image.txt"
cp "$root/build/configs/$config/defconfig" "$output/requested.config"
docker pull --platform linux/amd64 "$image" 2>&1 | tee "$output/image-pull.log"
docker image inspect "$image" > "$output/image-inspect.json"
image_id="$(docker image inspect --format '{{.Id}}' "$image")"

# No TTY or privileged container is required for compilation. Keep the normal
# /root/tizenrt mount used by dbuild.sh for vendor packaging scripts.
docker run --rm --pull=never --platform linux/amd64 --name "$builder" \
    -v "$root:/root/tizenrt" -w /root/tizenrt/os "$image_id" \
    bash -eo pipefail -c '
        arm-none-eabi-gcc --version
        make --version
        cd tools
        ./configure.sh "$1"
        cd ..
        # The top-level Makefile orders compilation/linking/post-processing as
        # sibling prerequisites. Parallelize configurations, not these stages.
        make
        case "$1" in
            qemu/*) test -s ../build/output/bin/tinyara ;;
            *) test -s ../build/output/bin/tinyara.axf ;;
        esac
    ' -- "$config" </dev/null 2>&1 | tee "$output/build.log"
