#!/usr/bin/env python3
"""Folds build/h1.wasm (and build/h1.asm.js, the plain-JavaScript fallback, when it exists) into
port/web/sim.html and writes the single-file simulator to dist/h1-simulator.html."""
import base64, os, re, sys

root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
src = open(os.path.join(root, "port/web/sim.html"), encoding="utf-8").read()
wasm = open(os.path.join(root, "build/h1.wasm"), "rb").read()
asm_path = os.path.join(root, "build/h1.asm.js")
asm = ""
if os.path.exists(asm_path):
    js = open(asm_path, encoding="utf-8").read()
    cut = js.index("var retasmFunc")
    asm = "function H1ASM(){\n" + js[:cut] + "\nreturn asmFunc({});\n}\n"
    assert "</script" not in asm
out = src.replace("__WASM_B64__", base64.b64encode(wasm).decode("ascii")).replace("/*__ASMJS__*/", asm)
dst = os.path.join(root, "dist/h1-simulator.html")
open(dst, "w", encoding="utf-8").write(out)
print("wrote dist/h1-simulator.html, %d KB (wasm %d KB, fallback %d KB)" % (len(out) // 1024, len(wasm) // 1024, len(asm) // 1024))
