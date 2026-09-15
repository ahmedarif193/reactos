<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Ahmed ARIF <arif193@gmail.com>
-->

# Mesa Gallium OpenGL ICD

`mesa_gallium.dll` is the common Windows WGL ICD. Its renderers depend on
the target architecture:

| Renderer | Backend | Architectures |
| --- | --- | --- |
| `vc4` | Raspberry Pi 3 GPU | ARM64 |
| `v3d` | Raspberry Pi 4/5 GPU | ARM64 |
| `llvmpipe` | LLVM-accelerated CPU rendering | AMD64 and ARM64 when `ENABLE_MESA_LLVMPIPE=ON` |
| `softpipe` | CPU rendering without LLVM | i386; AMD64 and ARM64 when `ENABLE_MESA_LLVMPIPE=OFF` |

Native i386, AMD64 and ARM64 builds using llvm-mingw Clang enable
`MESA_GALLIUM_FROM_SOURCE` by default. On ARM64, the normal profile contains
V3D, VC4, and Softpipe. Enabling `ENABLE_MESA_LLVMPIPE` replaces Softpipe with
LLVMpipe in that same DLL, producing exactly V3D, VC4, and LLVMpipe. The Pi
display drivers and the software `MSOGL` fallback all register
`mesa_gallium.dll`; the image packages one shared binary, not a second
`mesadrv.dll`. Hardware is tried in V3D-then-VC4 order before the CPU fallback.
`GALLIUM_DRIVER=llvmpipe` forces LLVMpipe in the optional profile.

The `mesa_gallium` target builds and stages the DLL. Mesa uses Release
settings even when ReactOS is Debug. ARM64 additionally builds its static
KMT/zlib dependencies in a nested Release configuration. i386 and AMD64
do not need those dependencies or the nested support build.
Native ARM64 and ARM64EC support builds reuse the main build's host tools;
the parent builds those tools before starting the support configuration.
The support build retains the separate Pi KMT transports. Softpipe and
LLVMpipe use the software presentation path.

The i386 and AMD64 source builds use native CMake. ARM64 uses Mesa's Meson
profile so the Windows V3D and D3DKMT path is included; LLVMpipe requires
Meson 1.12.0 or newer. Host requirements are CMake 3.24+, llvm-mingw, Meson
for ARM64, Ninja, Bison 2.7+, Flex, and Python with Mako, packaging and PyYAML.
Nested builds inherit the invoking `ninja -jN` or `cmake --build --parallel N`
at build time, including ARM64EC and WoW64. `CMAKE_BUILD_PARALLEL_LEVEL` is also
supported. The old fixed `MESA_BUILD_JOBS` cache setting is removed.
`MESA_BISON`, `MESA_FLEX`, and `MESA_PYTHON` select the host generator tools.
The ARM64 Meson build uses `MESA_MESON_JOBS` (default 8). The native CMake
output and ARM64 Meson output use separate build directories.
See [Mesa's CMake notes](../../../submodules/mesa/cmake/README.md) for standalone
build commands and supported profiles.

The ARM64 Meson WGL target links both Broadcom drivers and their ReactOS D3DKMT
winsys code. This establishes the combined build, but does not by itself prove
VC4 or V3D rendering on Raspberry Pi hardware.

`MESA_GALLIUM_FROM_SOURCE=OFF` disables the shared ICD and selects the
previous packaged Pi 3 ICD and custom Pi 5 ICD when those boards are enabled.
The FEX runtime builds a separate ARM64EC copy of the shared ICD. The nested
KMT support build disables the shared ICD to prevent recursive builds.
There is no separate Softpipe build option. Explicitly enabling
`ENABLE_MESA_LLVMPIPE` selects LLVMpipe inside the same ICD and also packages
the separate Lavapipe Vulkan ICD and Khronos loader.
Existing `RPI3VC4_MESA_FROM_SOURCE` caches seed the renamed
option on migration. Explicit `RPI3VC4_MESA_VC4_ICD` and
`RPI5VC4_MESA_V3D_ICD` paths override the shared ICD for the respective board
and retain their established filenames. The custom Pi 5 implementation is
also packaged as `rpi5vc4ogl_legacy.dll` when the modern ICD is selected.

The WGL integration supplies shared GPU surfaces and tracks content updates.
Software rendering keeps its GDI display target instead of entering GPU-only
DWM callbacks.
