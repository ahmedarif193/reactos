# PROJECT:     ReactOS Direct3D 12
# LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
# PURPOSE:     Build the vkd3d-backed Direct3D 12 frontend
# COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>

spec2def(d3d12.dll d3d12.spec ADD_IMPORTLIB)

add_library(d3d12 MODULE
    d3d12_main.c
    ${CMAKE_CURRENT_BINARY_DIR}/d3d12.def
    ${CMAKE_CURRENT_BINARY_DIR}/d3d12_stubs.c)

set_module_type(d3d12 win32dll ENTRYPOINT 0)
target_include_directories(d3d12 PRIVATE ${REACTOS_SOURCE_DIR}/sdk/lib/3rdparty/vkd3d/include)
target_link_libraries(d3d12 vkd3d dx10guid uuid wine)
add_importlibs(d3d12 dxgi d3dwine gdi32 user32 ole32 advapi32 msvcrt kernel32 ntdll)
add_dependencies(d3d12 d3d_idl_headers wineheaders)
add_cd_file(TARGET d3d12 DESTINATION reactos/system32 FOR all)
