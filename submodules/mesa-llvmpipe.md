<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Ahmed ARIF -->

# Modern Mesa in the ReactOS build

The vendored `submodules/mesa` source comes from
https://github.com/eotics-com/reactos-mesa.git at commit
`44504d1036b6f2355445404f63c4248bc01249d6`. Its upstream base is Mesa
**26.2.2**, commit `3281a69a8bfd9f997e91c15ed0e6290cae12dd32`, released
2026-09-02.
The original upstream is https://gitlab.freedesktop.org/mesa/mesa.git.
Mesa's licenses and authorship are unchanged. This integration note's GPL
notice does not relicense Mesa or LLVM.

The GitHub repository's `main` mirrors upstream development. The imported
`ros-dev` snapshot contains the ReactOS WGL, RPi3 D3DKMT and ARM64EC port
changes. A normal ReactOS checkout contains all source needed for the build.

## Enable and build

In menuconfig, choose **System components and compatibility -> Graphics
stack -> Modern Mesa LLVMpipe software OpenGL driver**. It defaults off.
The common CMake option is `ENABLE_MESA_LLVMPIPE`; both configure frontends
consume it. Current build support is llvm-mingw Clang, AMD64 or native ARM64.

For an already configured, compatible build directory:

```sh
cmake -S . -B output-Clang-arm64-debug -DENABLE_MESA_LLVMPIPE=ON
cmake --build output-Clang-arm64-debug --target mesa-llvmpipe
```

The same option on an AMD64 tree builds AMD64, not the host architecture.
`MESA_BUILD_JOBS` defaults to 4 to limit memory pressure. The first build needs
network access and several GB of free space. Required host tools are CMake 3.24+,
Ninja, Meson 1.12.0+, Python with Mako, packaging and PyYAML, and llvm-mingw.
Disabled builds do not probe these Mesa dependencies or download LLVM.
`MESA_MESON` can select a specific Meson installation without changing the
host's system Meson.

CMake builds static Windows LLVM **22.1.8** from its official, SHA-256-pinned
source archive, including the native TableGen tools needed for cross builds.
Mesa then builds with Meson using that target LLVM installation. Host LLVM
libraries must not be substituted for Windows libraries. A matching existing
static Windows LLVM installation can be supplied with `MESA_LLVM_ROOT` and
its `MESA_LLVM_LICENSE_FILE`.
Alternatively, `MESA_LLVM_SOURCE_ROOT` can reuse an already extracted LLVM
22.1.8 source tree across architectures, retaining separate target build and
install directories. Do not extract or update that shared source while a
consumer is building it.

Compiler caches are disabled for the external builds. Incremental outputs
live only under the selected build tree's `submodules/mesa-llvmpipe/`; source
is not copied into another Mesa session directory. The vendored source is
never patched or updated by CMake, and Meson fallback downloads are disabled.

## Outputs and boundaries

- `submodules/mesa-llvmpipe/mesadrv.dll`: the additional WGL ICD, LLVMpipe only,
  with static LLVM. Image builds package it in `reactos/system32`.
- `mesa-licenses.zip`: Mesa and LLVM licensing, packaged under
  `reactos/3rdParty`.
- No Mesa replacement `opengl32.dll`, D3D12 backend or Vulkan driver is built
  by this target. Existing ReactOS OpenGL and RPi hardware-driver paths remain.
- Enabled image builds register `mesadrv.dll` as the `MSOGL` software ICD.
  ReactOS tries this only when neither WDDM nor XPDM publishes an ICD; missing
  or unusable registration retains the built-in software fallback.

Building/packaging the DLL does **not** prove that ReactOS discovers the ICD
or that an application renders correctly. Native/ReactOS rendering tests and
Ladybird's end-to-end behavior remain separate work. No browser arguments or
environment workarounds are installed by this integration.

## Validation status

AMD64 and ARM64 CMake configuration and menuconfig option checks passed.
Both architectures' LLVM dependencies, `mesadrv.dll` and license bundles built
successfully from `ros-dev` commit `94fe6bf842ad179af1a49d7a3ac391e1e382077a`.
These results apply to that recorded revision; later source changes need
separate validation.
Meson 1.12.0 resolved the LLVM 22 detection failure seen with Meson 1.10.0.
The same WGL probe completed 22 checks without failures with these drivers on
native Windows 11 ARM64 (10.0.26100.1742) and ReactOS AMD64 with the software-ICD
loader change. Both reported Mesa 26.2.2 / LLVM 22.1.8 / OpenGL 4.6, rendered
the expected green pixel, and completed buffer swap.

The generated AMD64 registry includes the MSOGL entries. Full main-tree boot
image packaging remains unverified. The ReactOS runtime test used a separately
packaged test image and a diagnostic DLL alias, not the final `mesadrv.dll`
installation path. ARM64 ReactOS runtime has not been tested.

An additional ReactOS `wglMakeCurrent` correction rebinds an already-current
context when its destination DC changes. A paired 103-check presentation and
rebinding probe passes on native Windows and the corrected AMD64 test image;
the uncorrected image fails ten checks. In the recorded browser run,
Ladybird's original Qt graphics binaries visibly rendered
`https://example.com` without renderer arguments. This does not establish
full browser or video compatibility. The option remains disabled by default
pending broader validation.
