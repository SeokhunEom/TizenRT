#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
set -eu
mm_dir=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
os_dir=$(dirname "$mm_dir")
build_dir=$(mktemp -d "${TMPDIR:-/tmp}/trmm-host.XXXXXX")
trap 'rm -rf "$build_dir"' EXIT HUP INT TERM
mkdir -p "$build_dir/include/nuttx/mm" "$build_dir/include/nuttx/lib"
for header in mm/mm.h mm/kasan.h lib/math32.h; do
  ln -s "$os_dir/include/nuttx/$header" "$build_dir/include/nuttx/$header"
done
cc=${CC:-cc}
flags="-std=gnu11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -DTRMM_HOST_TEST -I$mm_dir/compat/tests/include -I$mm_dir/compat/include -I$build_dir/include -I$mm_dir -I$mm_dir/tizenrt/bridge"
for source in mm_initialize mm_lock mm_malloc_size mm_shrinkchunk mm_brkaddr mm_calloc mm_extend mm_free mm_mallinfo mm_malloc mm_foreach mm_memalign mm_realloc mm_zalloc mm_heapmember mm_checkcorruption; do
  "$cc" $flags -include "$mm_dir/compat/symbol_map.h" -c "$mm_dir/mm_heap/$source.c" -o "$build_dir/$source.o"
done
"$cc" $flags -include "$mm_dir/compat/symbol_map.h" -c "$mm_dir/tizenrt/bridge/nuttx_heap_bridge.c" -o "$build_dir/bridge.o"
"$cc" $flags -c "$mm_dir/compat/tests/host_os.c" -o "$build_dir/host_os.o"
"$cc" $flags -c "$mm_dir/compat/tests/test_heap.c" -o "$build_dir/test_heap.o"
"$cc" -fsanitize=address,undefined -pthread "$build_dir"/*.o -o "$build_dir/test_heap"
"$build_dir/test_heap"
