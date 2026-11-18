#!/bin/sh
# Builds the host port: build/h1host. Usage: tools/build_host.sh [extra cflags]
set -e
cd "$(dirname "$0")/.."
mkdir -p build
clang -std=c99 -O2 -g -Wall -Wextra -Wdouble-promotion -Wshadow -Wno-unused-parameter \
    -fno-fast-math -ffp-contract=off "$@" \
    core/*.c port/host/main.c -o build/h1host
echo "built build/h1host"
