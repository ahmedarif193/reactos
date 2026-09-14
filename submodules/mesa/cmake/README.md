# Native CMake build for ReactOS

This build compiles the vendored Mesa 26.2.2 Windows WGL configurations directly
with CMake. Configuration, compilation, generator execution and incremental
rebuilds do not invoke Meson or read Meson's build metadata. The upstream Meson
files remain for other platforms and drivers.

| Architecture | Default ICD | Optional ICD |
| --- | --- | --- |
| i386 | softpipe | — |
| AMD64 | softpipe | LLVMpipe with static Windows LLVM 22 |
| ARM64 | softpipe + VC4 | LLVMpipe with static Windows LLVM 22 |
| ARM64EC | softpipe + VC4 | — |

The imported Meson options also selected V3D on ARM64, but the WGL target did
not link or expose it. Its driver/winsys sources require Linux headers. This
port includes the Broadcom compiler and generators used by VC4, and does not
claim to provide the unported Windows V3D renderer.

## ReactOS build

Use the existing `MESA_GALLIUM_FROM_SOURCE` and `ENABLE_MESA_LLVMPIPE` options
and `mesa_gallium` / `mesa-llvmpipe` targets. Release optimization, assertion
settings, the separate Release KMT/zlib build, static C++ runtime linking,
DLL staging, stripping, and image registration are retained. The nested
ARM64EC and i386 builds use the same CMake entry point.

Required host tools are CMake 3.24+, Ninja, Python 3.8+ with Mako, packaging and
PyYAML, Bison 2.7+, and Flex. Compilers must be llvm-mingw Clang for Windows.
`MESA_BISON`, `MESA_FLEX`, `MESA_PYTHON` and `MESA_NINJA` select tools in the
ReactOS build. Standalone CMake uses the standard `BISON_EXECUTABLE`,
`FLEX_EXECUTABLE`, and `Python3_EXECUTABLE` variables. macOS's system Bison is
older than required; the ReactOS integration also searches Homebrew's Bison.

Mesa's new external build directory is `mesa-source/cmake-build` for the common
ICD and `mesa-llvmpipe/cmake-build` for LLVMpipe. Old Meson output directories
are unused and need not be removed to migrate an existing ReactOS build tree.

## Parallel builds

The ReactOS integration forwards the invoking `ninja -jN` or
`cmake --build --parallel N` at build time to Mesa, its support libraries,
optional LLVM, and the ARM64EC/WoW64 runtime subbuilds. Changing the count does
not require reconfiguration. The previous `MESA_BUILD_JOBS` cache is removed.
`CMAKE_BUILD_PARALLEL_LEVEL` supplies the count when no explicit ancestor
build option is found; otherwise the native generator chooses its default.
Ninja's unlimited `-j0` is preserved.

Ninja does not export its command-line job count. The launcher in
`sdk/cmake/build-with-parallel.py` reads the ancestor command arguments on
macOS, Linux and Windows, without additional Python packages. Hosts that
restrict process inspection can use `CMAKE_BUILD_PARALLEL_LEVEL` explicitly.
This forwards a per-build count; independent Ninja processes do not share a
global pool of job slots.

## Standalone build

For example, build the AMD64 softpipe ICD from the ReactOS source root:

```sh
cmake -S submodules/mesa -B output-mesa-cmake -G Ninja \
  -DCMAKE_SYSTEM_NAME=Windows -DCMAKE_SYSTEM_PROCESSOR=amd64 \
  -DCMAKE_C_COMPILER=/path/to/llvm-mingw/bin/x86_64-w64-mingw32-clang \
  -DCMAKE_CXX_COMPILER=/path/to/llvm-mingw/bin/x86_64-w64-mingw32-clang++ \
  -DCMAKE_ASM_COMPILER=/path/to/llvm-mingw/bin/x86_64-w64-mingw32-clang
cmake --build output-mesa-cmake --target mesa_gallium --parallel 4
```

The DLL is `src/gallium/targets/wgl/mesa_gallium.dll` under the build directory.
`cmake --install` installs it into `bin` under the chosen install prefix.
The optional `opengl32` target builds Mesa's loader for standalone testing;
ReactOS packages only the ICD and keeps its own OpenGL loader.

Set `MESA_ARCH` to `arm64`, `arm64ec`, `amd64`, or `i386` when it cannot be
inferred from `CMAKE_SYSTEM_PROCESSOR`. Select the matching compiler triple.
ARM64/ARM64EC additionally require `MESA_REACTOS_SOURCE_DIR` and
`MESA_REACTOS_BUILD_DIR`, with matching Release `rpi3vc4kmt`, `vc4kmt` and `zlib`
archives built by ReactOS. The parent build handles these dependencies.

For LLVMpipe, set `MESA_LLVMPIPE=ON`, `MESA_LLVM_ROOT` to the matching Windows
static LLVM 22 installation, `MESA_WGL_DLL_NAME=mesadrv`, and
`MESA_ASSERTIONS=OFF`. LLVMpipe does not require KMT/zlib. The native ARM64 and
AMD64 LLVM backends are selected from LLVM's CMake package. The default common
ICD keeps assertions enabled, including when optimized.

## Maintaining the port

`Targets.cmake` contains explicit source lists, per-target includes/options,
archive composition and DLL link dependencies. `CompileOptions.cmake` captures
the Windows/llvm-mingw feature configuration. `Generators.cmake` declares the
Python, Bison and Flex commands and their input dependencies. These are native,
editable CMake files: update them together with their corresponding upstream
Meson declarations when importing Mesa changes.

The lists were translated from fresh configurations of this source snapshot
for all six profiles, then restricted to the WGL DLL and loader dependency
graphs. Generated source and header contents remain produced by Mesa's own
scripts. `capture.py` handles generators that write to stdout, uses argument
arrays instead of a shell, and replaces the output only after success.
All generated files live in the build tree. A common generation prerequisite
prevents cross-library generated-header races; compiler depfiles handle
subsequent header dependencies.

The vendored snapshot's `src/git_sha1.h` is generated only when missing or
when its generator inputs change. To change `MESA_GIT_SHA1_OVERRIDE` in an
existing build, delete that header from the Mesa build directory and rebuild
with the desired environment value.

## Conversion validation (2026-09-14)

- Built the common ICD for ARM64, ARM64EC, i386 and AMD64 with llvm-mingw.
- Built AMD64 LLVMpipe against static Windows LLVM 22.1.8.
- Compared each of those five DLLs with its previous Meson output: all 52
  export names and ordinals match, and their imported DLL sets are unchanged.
- Built and staged the native ARM64 ICD through the main ReactOS target.
- Built the optional ARM64 `opengl32` loader and checked standalone installation.
- Checked incremental rebuilds, regeneration after deleting a generated source,
  generation with spaces in both source/build paths, and capture-helper failure
  cleanup. Generated-code differences were paths, Bison include guards and an
  old i386 Git version marker, rather than shader/parser content changes.

A full ARM64 LLVMpipe link was not validated because no ARM64 static LLVM
installation was available. Rendering on Windows/ReactOS and a complete OS
image build were not tested during this conversion. Matching exports and
imports establish build/ABI checks, not runtime rendering equivalence.

The parallel launcher has regression tests runnable with
`python3 sdk/cmake/tests/test_build_with_parallel.py` from the ReactOS source
root. They exercise real CMake/Ninja builds across two nested levels, including
paths with spaces, changes to `-j` without reconfiguration, environment
precedence, native defaults and `-j0`.
