<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Ahmed ARIF <arif193@gmail.com>
-->

# Mesa Gallium OpenGL ICD

`mesa_gallium.dll` is the common Windows WGL ICD for three renderers:

| Renderer | Backend |
| --- | --- |
| `vc4` | Raspberry Pi 3 GPU |
| `v3d` | Raspberry Pi 5 GPU |
| `softpipe` | CPU rendering, independent of Raspberry Pi hardware |

The Raspberry Pi 3/5 profile builds all three from the pinned
`submodules/mesa` revision. Both display drivers register `mesa_gallium.dll`,
so the image packages one shared binary. Hardware is preferred by default;
`GALLIUM_DRIVER=softpipe` forces software rendering without an LLVM dependency.

The `mesa_gallium` target builds and stages the DLL. Mesa and its static
KMT/zlib dependencies use Release settings even when ReactOS is Debug.
VC4 and V3D retain separate KMT transports because their kernel interfaces
differ. Softpipe uses the software presentation path.

Initialize the submodule before configuring:

    git submodule update --init --depth 1 -- submodules/mesa

The source build requires llvm-mingw, Meson, Ninja, and Python with Mako,
packaging and PyYAML. `MESA_BUILD_JOBS` limits parallel jobs and defaults to
four. Set `MESA_MESON` if Meson is outside `PATH`.

`MESA_GALLIUM_FROM_SOURCE=OFF` selects the previous packaged Pi 3 ICD and
custom Pi 5 ICD. Existing `RPI3VC4_MESA_FROM_SOURCE` caches seed the renamed
option on migration. Explicit `RPI3VC4_MESA_VC4_ICD` and
`RPI5VC4_MESA_V3D_ICD` paths override the shared ICD for the respective board
and retain their established filenames. The custom Pi 5 implementation is
also packaged as `rpi5vc4ogl_legacy.dll` when the modern ICD is selected.

The WGL integration supplies shared GPU surfaces and tracks content updates.
Software rendering keeps its GDI display target instead of entering GPU-only
DWM callbacks.
