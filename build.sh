#!/bin/sh
set -eu
CC=${CC:-clang}
CFLAGS=${CFLAGS:--Wall -Wextra -g}
LFLAGS=${LFLAGS:-}
cd "$(dirname "$0")"
echo "Entering directory '$PWD'"
set -x
$CC $CFLAGS $LFLAGS -o zoo-lzw-decompress zoo-lzw-decompress.c
