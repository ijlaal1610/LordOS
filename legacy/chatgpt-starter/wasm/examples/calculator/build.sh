#!/bin/sh
set -eu
cd "$(dirname "$0")"
if ! command -v clang >/dev/null 2>&1; then
  echo 'Install clang with WebAssembly target support.' >&2
  exit 1
fi
clang --target=wasm32 -O2 -nostdlib -Wl,--no-entry -Wl,--export=add -Wl,--export=multiply -o calculator.wasm calculator.c
echo 'Built calculator.wasm (load from JS WebAssembly.instantiate).'
