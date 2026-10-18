#!/bin/sh
# Compila los módulos de prueba (necesita clang y lld; ver docs/WASM.md, "Paquetes en la VM").
set -e
cd "$(dirname "$0")"
clang --target=wasm32 -O2 -nostdlib -mbulk-memory -msign-ext -mnontrapping-fptoint \
      -Wl,--no-entry -Wl,--allow-undefined -Wl,--export-memory -o demo.wasm demo.c
wat2wasm api.wat -o api.wasm
