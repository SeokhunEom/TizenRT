#!/bin/sh

# SPDX-License-Identifier: Apache-2.0

# Verify that the imported Apache NuttX ostest sources differ only by the
# include adaptations documented in PORTING.md.

set -eu

if [ "$#" -ne 1 ]; then
	printf 'usage: %s /path/to/nuttx-apps/testing/ostest\n' "$0" >&2
	exit 2
fi

source_dir=$1
target_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
temp_dir=$(mktemp -d "${TMPDIR:-/tmp}/tizenrt-ostest-verify.XXXXXX")
trap 'rm -r "$temp_dir"' EXIT HUP INT TERM

count=0
for source_file in "$source_dir"/*.[ch]; do
	base_name=$(basename -- "$source_file")
	normalized="$temp_dir/$base_name"

	sed 's#<nuttx/#<tinyara/#g; s#<tinyara/debug.h>#<debug.h>#g' \
		"$source_file" > "$normalized.base"

	if [ "$base_name" = ostest_main.c ]; then
		awk 'BEGIN { changed=0 }
			!changed && $0 == "#include <malloc.h>" {
				print "#include <stdlib.h>"; changed=1; next
			}
			{ print }' "$normalized.base" > "$normalized"
		rm "$normalized.base"
	else
		mv "$normalized.base" "$normalized"
	fi

	cmp "$normalized" "$target_dir/$base_name"
	count=$((count + 1))
done

printf 'verified %d imported .c/.h files\n' "$count"
