#!/bin/sh
# Builds the browser core: build/h1.wasm (clang's wasm32 target and wasm-ld, no emscripten),
# optionally a plain-JavaScript fallback with binaryen's wasm2js (set WASM2JS to its path),
# then tools/pack_web.py folds both into the single-file simulator page in dist/.
set -e
cd "$(dirname "$0")/.."
mkdir -p build dist
clang --target=wasm32 -std=c99 -O2 -ffreestanding -fno-builtin -nostdlib -mnontrapping-fptoint -mbulk-memory \
    -Wall -Wextra -Wdouble-promotion -Wshadow -Wno-unused-parameter -fno-fast-math -ffp-contract=off \
    -Wl,--no-entry -Wl,--strip-all -Wl,-z,stack-size=131072 \
    core/*.c port/web/glue.c -o build/h1.wasm
if [ -n "$WASM2JS" ]; then
    "$WASM2JS" -O2 --enable-nontrapping-float-to-int --enable-bulk-memory build/h1.wasm -o build/h1.asm.js
fi
python3 tools/pack_web.py
