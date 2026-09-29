#!/bin/bash
# Builds and runs host unit tests with warnings as errors and sanitizers.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
src=${1:-$here/../../src}
out=$(mktemp -d)
gcc -std=c99 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I"$src" "$here/test_vita_loadorder.c" "$src/vita/vita_loadorder.c" -o "$out/t"
"$out/t"
