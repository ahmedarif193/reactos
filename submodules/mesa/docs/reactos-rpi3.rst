.. SPDX-License-Identifier: GPL-3.0-or-later
.. SPDX-FileCopyrightText: 2026 Ahmed ARIF

ReactOS RPi3 development branch
==============================

``ros-dev`` starts at upstream Mesa 26.2.2,
``3281a69a8bfd9f997e91c15ed0e6290cae12dd32``. This document does not
change the licenses of the imported Mesa code.

Port provenance
---------------

The VC4 D3DKMT backend and its Windows/WGL prerequisites are ported from
``https://github.com/eotics-com/mesa``, branch ``reactos-v3d-d3dkmt``,
snapshot ``86e4fffe990070b0d1044d72fcd6104ea542ea80``. The original
upstream base was ``e7f6c8ab7ed2ff61185caa3082c693284b0cbcaa``.

The principal source commits are:

* ``ab61fd210202185323102b664ffcf63094692913``: Windows DRM types.
* ``e3e9e21e399f0221a33bd1dad54ee8eee3c63c97``: shared build options
  and Broadcom Windows declarations, without its V3D backend.
* ``5b8c5a45822ebdde341b063dd3108b0e1f1db20a``: WGL composition.
* ``810430d364ed88a5391e6507bbfcd8570ff79708``: VC4 backend.
* ``a9795d592173ad11466d4a757fa2db198b573f42``: VC4 WGL selection,
  recycled-DC handling and the separate sized present entry point.

Original source authorship and license notices are retained. The RPi5/V3D
Gallium backend is not imported. Three-way integration retains the newer
upstream logging, command-list interfaces and framebuffer lookup changes.
VC4-specific shared-surface callbacks are not installed in LLVMpipe-only
builds. Failed VC4 screen creation leaves device ownership with its winsys.

Local ReactOS patches
---------------------

``dll/opengl/rpi3vc4ogl/arm64-tile-read.patch`` is imported unchanged in
its own commit: paired ARM64 utile loads, early-clobber destination
registers and a memory clobber.

``dll/opengl/rpi3vc4ogl/wgl-present-fix.patch`` is adapted to the imported
port. Present callbacks use version 2 and a window-relative client
rectangle. The legacy ``PRESENTBUFFERS`` structure was already restored
by the old fork, so those obsolete hunks are not replayed. Its separate,
size/version-checked ``DrvPresentBuffers2`` interface is retained;
legacy callers never supply or expose trailing event fields.

A separate compiler-compatibility follow-up widens the four ARM64 assembly
stride operands to ``uintptr_t``. This gives the post-index addressing
instructions an explicitly zero-extended, 64-bit register value instead of
passing a 32-bit C operand to an X-register use.

Focused checks passed: 7,488 byte-copy/canary cases on the ARM64 macOS host,
strict Windows ARM64 compilation of that probe, and ARM64/AMD64 compile-time
checks of the 32-byte legacy and 48-byte sized WGL present layouts. These
are CPU/compiler/ABI checks, not Windows or ReactOS rendering tests and
not a Cortex-A53 non-cacheable-memory performance measurement.

Build and validation boundary
-----------------------------

Use a Windows ARM64 llvm-mingw cross file, ``-Dplatforms=windows``,
``-Dgallium-drivers=vc4``, ``-Dgallium-wgl-dll-name=rpi3vc4ogl``,
``-Dllvm=disabled``, and the ``reactos-source-dir`` / ``reactos-build-dir``
options. The matching ReactOS build must provide ``librpi3vc4kmt.a``.
The LLVMpipe build remains a separate configuration and DLL.

This import does not replace ReactOS's packaged prebuilt RPi3 ICD. Its
later shared-texture, tracing, swap-hint and GPU-window-copy increments
referenced in ReactOS's RPi3 README were not available in this checkout
and are not claimed to be reproduced by this branch.

Configuration or compilation alone is not native Windows, ReactOS or
physical RPi3 rendering proof. Existing port limitations still require
review, including device-registry lifetime during concurrent teardown,
waits under the device mutex, shared-surface geometry assumptions and
direct-primary copies racing window movement. Historical performance
results from the old fork do not validate this Mesa 26.2.2 port.
