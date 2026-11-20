#!/bin/sh
# h1's crash hunt (tests/fuzz.c): the core under the compiler's address and undefined-behaviour
# checkers, fed any saved state and any panel input. Usage: sh tests/run_fuzz.sh [SEEDS [FIRST]]
# A seed takes most of a minute; they run side by side, one a processor.
set -eu
cd "$(dirname "$0")/.."
CC="${CC:-cc}"
mkdir -p build
$CC -std=c99 -O1 -g -fno-omit-frame-pointer -ffp-contract=off \
    -fsanitize=address,undefined,float-cast-overflow,float-divide-by-zero -fno-sanitize-recover=all \
    -DH1_FUZZ=1 -w \
    core/*.c tests/fuzz.c -o build/h1fuzz -lpthread
n="${1:-4}"
first="${2:-1}"
export UBSAN_OPTIONS=print_stacktrace=1 ASAN_OPTIONS=detect_leaks=0
seq "$first" "$((first + n - 1))" | xargs -P "$(nproc 2>/dev/null || sysctl -n hw.ncpu)" -I{} build/h1fuzz 1 {}
echo "fuzz: $n seeds from $first, nothing found"
