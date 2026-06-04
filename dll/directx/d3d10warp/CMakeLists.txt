
spec2def(d3d10warp.dll d3d10warp.spec ADD_IMPORTLIB)

add_library(d3d10warp MODULE
    d3d10warp_main.c
    ${CMAKE_CURRENT_BINARY_DIR}/d3d10warp_stubs.c
    ${CMAKE_CURRENT_BINARY_DIR}/d3d10warp.def)

set_module_type(d3d10warp win32dll)
add_importlibs(d3d10warp msvcrt kernel32 ntdll)
add_cd_file(TARGET d3d10warp DESTINATION reactos/system32 FOR all)
