#!/bin/bash
# Builds and runs host unit tests with warnings as errors and sanitizers.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
src=${1:-$here/../../src}
out=$(mktemp -d)
cc="gcc -std=c99 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -I$src"
$cc "$here/test_vita_loadorder.c" "$src/vita/vita_loadorder.c" -o "$out/loadorder"
$cc "$here/test_vita_path.c" "$src/vita/vita_path.c" -o "$out/path"
rc=0
"$out/loadorder" || rc=1
"$out/path" || rc=1
exit $rc
