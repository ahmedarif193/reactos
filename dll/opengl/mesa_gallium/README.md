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
| `softpipe` | CPU rendering | i386, AMD64, ARM64 |

Native i386, AMD64 and ARM64 builds using llvm-mingw Clang enable
`MESA_GALLIUM_FROM_SOURCE` by default. i386 and AMD64 build softpipe;
ARM64 adds VC4 from the vendored `submodules/mesa` snapshot, including
generic, Raspberry Pi 3 and Raspberry Pi 5 configurations. Both Pi display
drivers register `mesa_gallium.dll`; the same DLL is registered as the `MSOGL`
fallback on displays without a hardware ICD. The image packages one shared
binary. Hardware is preferred by default;
`GALLIUM_DRIVER=softpipe` forces software rendering without an LLVM dependency.

The `mesa_gallium` target builds and stages the DLL. Mesa uses Release
settings even when ReactOS is Debug. ARM64 additionally builds its static
KMT/zlib dependencies in a nested Release configuration. i386 and AMD64
do not need those dependencies or the nested support build.
Native ARM64 and ARM64EC support builds reuse the main build's host tools;
the parent builds those tools before starting the support configuration.
The support build retains the separate Pi KMT transports. Softpipe uses the
software presentation path.

The source build uses native CMake and requires CMake 3.24+, llvm-mingw,
Ninja, Bison 2.7+, Flex, and Python with Mako, packaging and PyYAML.
Nested builds inherit the invoking `ninja -jN` or `cmake --build --parallel N`
at build time, including ARM64EC and WoW64. `CMAKE_BUILD_PARALLEL_LEVEL` is also
supported. The old fixed `MESA_BUILD_JOBS` cache setting is removed.
`MESA_BISON`, `MESA_FLEX`, and `MESA_PYTHON` select the host generator tools.
Meson is no longer required. Existing Meson build directories can coexist
with the new `mesa-source/cmake-build` directory; packaging uses the CMake DLL.
See [Mesa's CMake notes](../../../submodules/mesa/cmake/README.md) for standalone
build commands and supported profiles.

The imported Meson configuration selected V3D, but its WGL target did not
link or expose the V3D renderer. The V3D driver and DRM winsys still require
Linux headers and are not part of the working Windows WGL dependency graph.
The native CMake build preserves that behavior; it does not establish Pi 5
hardware rendering. The Broadcom compiler and packet generators needed by
VC4 remain included.

`MESA_GALLIUM_FROM_SOURCE=OFF` disables the shared ICD and selects the
previous packaged Pi 3 ICD and custom Pi 5 ICD when those boards are enabled.
The FEX runtime builds a separate ARM64EC copy of the shared ICD. The nested
KMT support build disables the shared ICD to prevent recursive builds.
There is no separate softpipe build option. Explicitly enabling
`ENABLE_MESA_LLVMPIPE` selects that additional ICD as the software fallback.
Existing `RPI3VC4_MESA_FROM_SOURCE` caches seed the renamed
option on migration. Explicit `RPI3VC4_MESA_VC4_ICD` and
`RPI5VC4_MESA_V3D_ICD` paths override the shared ICD for the respective board
and retain their established filenames. The custom Pi 5 implementation is
also packaged as `rpi5vc4ogl_legacy.dll` when the modern ICD is selected.

The WGL integration supplies shared GPU surfaces and tracks content updates.
Software rendering keeps its GDI display target instead of entering GPU-only
DWM callbacks.
