#!/usr/bin/env bash
# Build fexbench for ARM64 and x86-64 from one source, into a payload folder
# that modules/fexbench deploys and boot-starts.
#
#   ./build-fexbench.sh [out-dir]      -> out-dir/fexbench_{arm64,x86_64}.exe
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT="${1:-$HERE/payload}"
TOOLCHAIN="${ROSBE_LLVM_MINGW:-$HOME/.local/opt/rosbe/llvm-mingw}/bin"

[ -x "$TOOLCHAIN/aarch64-w64-mingw32-clang" ] || { echo "missing $TOOLCHAIN/aarch64-w64-mingw32-clang" >&2; exit 1; }
[ -x "$TOOLCHAIN/x86_64-w64-mingw32-clang" ] || { echo "missing $TOOLCHAIN/x86_64-w64-mingw32-clang" >&2; exit 1; }

mkdir -p "$OUT"
"$TOOLCHAIN/aarch64-w64-mingw32-clang" -O2 -o "$OUT/fexbench_arm64.exe" "$HERE/fexbench.c"
"$TOOLCHAIN/x86_64-w64-mingw32-clang"  -O2 -o "$OUT/fexbench_x86_64.exe" "$HERE/fexbench.c"
echo "[fexbench] built:"
ls -la "$OUT"/fexbench_*.exe
