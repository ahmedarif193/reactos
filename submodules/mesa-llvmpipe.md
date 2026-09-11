<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Ahmed ARIF -->

# Modern Mesa in the ReactOS build

The `mesa` submodule uses https://github.com/eotics-com/reactos-mesa.git.
Its initial pin is upstream Mesa **26.2.2**, commit
`3281a69a8bfd9f997e91c15ed0e6290cae12dd32`, released 2026-09-02.
The original upstream is https://gitlab.freedesktop.org/mesa/mesa.git.
Mesa's licenses and authorship are unchanged. This integration note's GPL
notice does not relicense Mesa or LLVM.

The GitHub repository's `main` mirrors upstream development; the ReactOS
gitlink deliberately pins a release instead of following that moving branch.
The separate `eotics-com/mesa:reactos-v3d-d3dkmt` changes and later local RPi3
patches have not been merged into this new repository.

## Enable and build

Initialize only Mesa (do not recursively initialize unrelated submodules):

```sh
git submodule update --init --depth 1 -- submodules/mesa
```

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
Ninja, Meson, Python with Mako, packaging and PyYAML, and llvm-mingw. Disabled
builds do not probe these Mesa dependencies or download LLVM.

CMake builds static Windows LLVM **22.1.8** from its official, SHA-256-pinned
source archive, including the native TableGen tools needed for cross builds.
Mesa then builds with Meson using that target LLVM installation. Host LLVM
libraries must not be substituted for Windows libraries. A matching existing
static Windows LLVM installation can be supplied with `MESA_LLVM_ROOT` and
its `MESA_LLVM_LICENSE_FILE`.

Compiler caches are disabled for the external builds. Incremental outputs
live only under the selected build tree's `submodules/mesa-llvmpipe/`; source
is not copied into another Mesa session directory. The source submodule is
never patched or updated by CMake, and Meson fallback downloads are disabled.

## Outputs and boundaries

- `submodules/mesa-llvmpipe/mesadrv.dll`: the additional WGL ICD, LLVMpipe only,
  with static LLVM. Image builds package it in `reactos/system32`.
- `mesa-licenses.zip`: Mesa and LLVM licensing, packaged under
  `reactos/3rdParty`.
- No Mesa replacement `opengl32.dll`, D3D12 backend or Vulkan driver is built
  by this target. Existing ReactOS OpenGL and RPi hardware-driver paths remain.

Building/packaging the DLL does **not** prove that ReactOS discovers the ICD
or that an application renders correctly. Normal software-ICD registration
and loader integration, native/ReactOS rendering tests, and Ladybird's
end-to-end behavior are separate work. No browser arguments or environment
workarounds are installed by this integration.

## Validation status

AMD64 and ARM64 CMake configuration and menuconfig option checks passed.
The ARM64 LLVM dependency compiled and installed, but the first Mesa
configuration with Meson 1.10.0 failed during LLVM discovery: its generated
CMake lookup reported `find_package called with invalid argument "unknown
version"`. The full `mesadrv.dll` build and image packaging are therefore not
yet verified. The option remains disabled by default; this is build
integration in progress, not a validated runtime driver.
