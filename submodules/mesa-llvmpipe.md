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
stack -> Modern Mesa LLVMpipe OpenGL and Lavapipe Vulkan**.
It defaults off.
The common CMake option is `ENABLE_MESA_LLVMPIPE`; both configure frontends
consume it. Current build support is llvm-mingw Clang, AMD64 or native ARM64,
with `MESA_GALLIUM_FROM_SOURCE` enabled.

For an already configured, compatible build directory:

```sh
cmake -S . -B output-Clang-arm64-debug -DENABLE_MESA_LLVMPIPE=ON
cmake --build output-Clang-arm64-debug --target mesa-llvmpipe
```

The same option on an AMD64 tree builds AMD64, not the host architecture.
Mesa and LLVM inherit the invoking build's `-jN` / `--parallel N` at runtime;
`CMAKE_BUILD_PARALLEL_LEVEL` is also supported. There is no separate fixed Mesa
job limit. The first build needs
network access and several GB of free space. Required host tools are CMake 3.24+,
Meson, Ninja, Bison 2.7+, Flex, Python with Mako, packaging and PyYAML, and llvm-mingw.
Disabled builds do not probe these optional dependencies or download LLVM.
`MESA_BISON`, `MESA_FLEX`, and `MESA_PYTHON` select host generator tools.
The AMD64 OpenGL sub-build uses CMake. Native ARM64 OpenGL and Lavapipe use
Meson; the ARM64 WGL build requires Meson 1.12.0 or newer when LLVM is enabled.

CMake builds static Windows LLVM **22.1.8** from its official, SHA-256-pinned
source archive, including the native TableGen tools needed for cross builds.
The Vulkan loader and header archives are likewise pinned to Khronos release
**1.4.354**, matching Mesa's Vulkan headers. Microsoft DirectX-Headers
**1.619.1** supplies the build-only Windows WSI declarations.
Both Mesa builds use that target LLVM installation. Host LLVM libraries must
not be substituted for Windows libraries. A matching existing static Windows
LLVM installation can be supplied with `MESA_LLVM_ROOT` and its
`MESA_LLVM_LICENSE_FILE`.
Alternatively, `MESA_LLVM_SOURCE_ROOT` can reuse an already extracted LLVM
22.1.8 source tree across architectures, retaining separate target build and
install directories. Do not extract or update that shared source while a
consumer is building it.

Compiler caches are disabled for the external builds. LLVM, Lavapipe, the
Vulkan loader, and their dependencies live under the selected build tree's
`submodules/mesa-llvmpipe/`. The shared WGL ICD remains in the normal
`dll/opengl/mesa_gallium/mesa-source/` build. The vendored Mesa source is not
copied, patched, or updated by CMake, and Mesa's dependency fallbacks are
disabled.

## Outputs and boundaries

- `dll/opengl/mesa_gallium/mesa-icd/mesa_gallium.dll`: the single WGL ICD.
  On native ARM64 it contains V3D, VC4, and LLVMpipe with static LLVM, in that
  default order. Thus Raspberry Pi hardware is tried first and LLVMpipe is the
  CPU fallback. `GALLIUM_DRIVER=llvmpipe` forces the CPU driver. Softpipe is
  not compiled into this optional ARM64 configuration.
- `vulkan_lvp.dll` and `lvp_icd.json`: Mesa Lavapipe's software Vulkan ICD
  and loader manifest, packaged in `reactos/system32`.
- `vulkan-1.dll`: Khronos Vulkan Loader 1.4.354, packaged in
  `reactos/system32`. The small ReactOS adaptation expands environment strings
  in machine-wide ICD registry value names, allowing an image-independent
  `%SystemRoot%` path.
- `mesa-licenses.zip`: Mesa, LLVM, Vulkan-Headers, Vulkan-Loader and
  DirectX-Headers licensing, packaged under
  `reactos/3rdParty`.
- No Mesa replacement `opengl32.dll` or D3D12 backend is built by this target.
  Existing ReactOS OpenGL and RPi hardware-driver paths remain.
- Enabled image builds register `mesa_gallium.dll` as the `MSOGL` fallback ICD.
  ReactOS tries this only when neither WDDM nor XPDM publishes an ICD; missing
  or unusable registration retains the built-in software fallback.
- Enabled image builds register the Lavapipe manifest in the standard
  machine-wide Vulkan Drivers key and also set `VK_ADD_DRIVER_FILES` for
  non-elevated processes. Both are additive: a later Intel or other vendor
  Vulkan ICD remains visible to the same Khronos loader.

Building/packaging the DLL does **not** prove that ReactOS discovers the ICD or
that an application renders correctly. A focused ARM64 ReactOS QEMU probe on
2026-09-15 loaded the packaged Vulkan loader, resolved the machine-registered
manifest to `X:\reactos\System32\lvp_icd.json`, created an instance, and
enumerated one CPU device: `llvmpipe (LLVM 22.1.8, 128 bits)`, Vulkan 1.4.354.
That proves loader/ICD initialization and device enumeration, not surface or
rendering behavior. Native comparison and application rendering remain
separate work. A second focused probe against the final LiveCD forced
`GALLIUM_DRIVER=llvmpipe`, loaded the registered `mesa_gallium.dll`, reported
OpenGL 4.6 / Mesa 26.2.2 with LLVM 22.1.8, rendered and read back the expected
green pixel, and completed 17 checks with no failures. That proves the installed
ARM64 LLVMpipe WGL path, not VC4 or V3D hardware rendering. No browser arguments
or environment workarounds are installed by this integration.

## Historical runtime validation

The results below predate the native CMake conversion and do not establish
runtime equivalence for the new build. See `mesa/cmake/README.md` for its
build validation.

AMD64 and ARM64 CMake configuration and menuconfig option checks passed.
Both architectures' LLVM dependencies, the former `mesadrv.dll`, and license bundles built
successfully from `ros-dev` commit `94fe6bf842ad179af1a49d7a3ac391e1e382077a`.
These results apply to that recorded revision; later source changes need
separate validation.
Meson 1.12.0 resolved the LLVM 22 detection failure seen with Meson 1.10.0.
The same WGL probe completed 22 checks without failures with these drivers on
native Windows 11 ARM64 (10.0.26100.1742) and ReactOS AMD64 with the software-ICD
loader change. Both reported Mesa 26.2.2 / LLVM 22.1.8 / OpenGL 4.6, rendered
the expected green pixel, and completed buffer swap.

The generated AMD64 registry includes the MSOGL entries. Full main-tree boot
image packaging was unverified in that historical test. It used a separately
packaged test image and a diagnostic DLL alias, not today's consolidated
`mesa_gallium.dll` installation path. The focused ARM64 LLVMpipe result above
supersedes that limitation for the CPU path only.

An additional ReactOS `wglMakeCurrent` correction rebinds an already-current
context when its destination DC changes. A paired 103-check presentation and
rebinding probe passes on native Windows and the corrected AMD64 test image;
the uncorrected image fails ten checks. In the recorded browser run,
Ladybird's original Qt graphics binaries visibly rendered
`https://example.com` without renderer arguments. This does not establish
full browser or video compatibility. The option remains disabled by default
pending broader validation.
