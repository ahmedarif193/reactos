# ARM64EC NTDLL bridge

`ntdll_chpe.dll` gives AMD64 callers ARM64EC entry thunks for native NTDLL
exports. Its public export names and ordinals must match the native ARM64
`ntdll.dll`; the bridge also has two bridge-only emulation entry points.

To refresh the bridge after native exports or headers change, build native
`ntdll` first, then run:

```sh
python3 dll/ntdll/generate_chpe_bridge.py \
  --build-dir output-Clang-arm64-debug/_fex_arm64ec \
  --scope all --write --report /tmp/chpe-bridge-audit.json
ninja -C output-Clang-arm64-debug/_fex_arm64ec ntdll_chpe
python3 dll/ntdll/generate_chpe_bridge.py \
  --build-dir output-Clang-arm64-debug/_fex_arm64ec \
  --scope all --verify-pe --report /tmp/chpe-bridge-audit.json
```

The helper reads native `ntdll.def`, the manual section of `chpebridge.spec`,
the native `.spec`, and Clang declarations under the ARM64EC build flags. It
regenerates the marked `.spec` section and `chpebridge_generated.inc`. A
nonzero exit means an export lacks a wrapper, a built PE export is missing,
or an ordinal differs. The JSON report lists generated exports, unresolved
names, and generated wrappers with callback-shaped parameters.

Typed forwarding covers ordinary calls. ABI-sensitive calls, exception and
thread entry points, variadic functions, and callbacks need explicit bridge
code. A native callback recipient must not invoke an AMD64 function pointer
as ARM64 code. The callback review list is a semantic audit queue; export
parity alone does not establish callback safety.
