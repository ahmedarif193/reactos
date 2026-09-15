# Native CMake build for ReactOS

This build compiles the vendored Mesa 26.2.2 Windows WGL and Lavapipe
configurations directly with CMake. Configuration, compilation, generator
execution and incremental rebuilds do not invoke or read another build
system's metadata. ReactOS uses this project for i386, AMD64, ARM64 and
ARM64EC.

| Architecture | Option disabled | `ENABLE_MESA_LLVMPIPE=ON` |
| --- | --- | --- |
| i386 | V3D + Softpipe | unsupported |
| AMD64 | Softpipe | LLVMpipe in the same ICD |
| ARM64 | V3D + VC4 + Softpipe | V3D + VC4 + LLVMpipe in the same ICD |
| ARM64EC | V3D + VC4 + Softpipe | unsupported |

The native ARM64 WGL target links V3D and VC4, both ReactOS D3DKMT winsyses,
the Broadcom compiler and versioned V3D support libraries. The i386 target
links V3D and its ReactOS D3DKMT winsys for WoW64 applications, without the
legacy VC4 driver. LLVMpipe replaces Softpipe when enabled but does not remove
either ARM64 Raspberry Pi hardware driver.

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

The native CMake external build directory is `mesa-source/cmake-build` for all
architectures. WGL and Lavapipe share that build directory. Optional LLVM,
Vulkan-loader, Vulkan-Headers and DirectX-Headers dependencies use the separate
`mesa-llvmpipe` work directory.

## Parallel builds

The native CMake ReactOS integration forwards the invoking `ninja -jN` or
`cmake --build --parallel N` at build time to Mesa, its support libraries,
optional LLVM, and the ARM64EC/WoW64 runtime subbuilds. Changing the count does
not require reconfiguration. The previous fixed build-job cache settings are
removed.
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
archives built by ReactOS. The i386 V3D target requires matching Release
`vc4kmt` and `zlib` archives. The parent build handles these dependencies.

For a standalone LLVMpipe build, set `MESA_LLVMPIPE=ON`, `MESA_LLVM_ROOT` to
the matching Windows static LLVM 22 installation, and choose
`MESA_WGL_DLL_NAME`. Add `MESA_LAVAPIPE=ON` and
`MESA_DIRECTX_HEADERS_ROOT=/path/to/DirectX-Headers/install` to build
`vulkan_lvp.dll` and `lvp_icd.json` from the same configuration. The native
ARM64 and AMD64 LLVM backends are selected from LLVM's CMake package. ReactOS
Release builds disable assertions; non-Release parent builds retain them while
still compiling optimized Mesa binaries.

## Maintaining the port

`Targets.cmake` contains explicit source lists, per-target includes/options,
archive composition and DLL link dependencies. `CompileOptions.cmake` captures
the Windows/llvm-mingw feature configuration. `Generators.cmake` declares the
Python, Bison and Flex commands and their input dependencies. These are native,
editable CMake files: update their source and generator lists when importing
Mesa changes.

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

## Conversion validation (2026-09-14 through 2026-09-15)

- Built the common ICD for ARM64, ARM64EC, i386 and AMD64 with llvm-mingw.
- Built AMD64 LLVMpipe against static Windows LLVM 22.1.8.
- Compared each of those five DLLs with its previous build output: all 52
  export names and ordinals match, and their imported DLL sets are unchanged.
- The original ARM64 CMake conversion did not yet contain V3D. On 2026-09-15,
  the V3D driver, D3DKMT winsys and performance-counter libraries were added;
  an ARM64 V3D + VC4 + LLVMpipe WGL DLL linked successfully.
- Built AMD64 and ARM64 Lavapipe DLLs from the same CMake configurations as
  their WGL DLLs. This is build evidence, not runtime or hardware proof.
- Built the optional ARM64 `opengl32` loader and checked standalone installation.
- Checked incremental rebuilds, regeneration after deleting a generated source,
  generation with spaces in both source/build paths, and capture-helper failure
  cleanup. Generated-code differences were paths, Bison include guards and an
  old i386 Git version marker, rather than shader/parser content changes.

Rendering on Windows/ReactOS and a complete OS image build were not repeated
for the 2026-09-15 CMake-only transition. Matching exports and imports plus the
AMD64/ARM64 links establish build and ABI checks, not runtime equivalence or
Raspberry Pi hardware rendering.

The parallel launcher has regression tests runnable with
`python3 sdk/cmake/tests/test_build_with_parallel.py` from the ReactOS source
root. They exercise real CMake/Ninja builds across two nested levels, including
paths with spaces, changes to `-j` without reconfiguration, environment
precedence, native defaults and `-j0`.
