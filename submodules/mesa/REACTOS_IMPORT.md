# ReactOS Mesa source snapshot

This directory is tracked directly by the ReactOS repository. It does not
require a Git submodule checkout.

- Source repository: https://github.com/eotics-com/reactos-mesa.git
- Imported commit: `44504d1036b6f2355445404f63c4248bc01249d6`
- Mesa release line: 26.2.2

The imported commit contains the ReactOS WGL, VC4, V3D, softpipe, and ARM64EC
port changes used by the build integration in `dll/opengl/mesa_gallium`.

The ReactOS WGL configurations now use the native `CMakeLists.txt` in this
snapshot; see `cmake/README.md`. The original Meson files remain available for
upstream configurations. The imported WGL target supports VC4 and software
rendering; its V3D selection did not provide a linked Windows V3D renderer.
