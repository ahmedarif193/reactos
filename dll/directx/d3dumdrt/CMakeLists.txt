# The runtime half is compiled at the same effective UMD interface version as
# the drivers it serves. Lower WDDM configurations therefore get the native
# shorter callback table; higher configurations retain the honest WDDM 2.0
# implementation ceiling instead of exposing zero-filled newer tails.
remove_definitions(-DDXGKDDI_INTERFACE_VERSION=0x1052)
add_definitions(-DDXGKDDI_INTERFACE_VERSION=0x11007)

spec2def(d3dumdrt.dll d3dumdrt.spec)

add_library(d3dumdrt MODULE
    d3dumdrt.c
    ${CMAKE_CURRENT_BINARY_DIR}/d3dumdrt.def)

set_module_type(d3dumdrt win32dll ENTRYPOINT DllMain 12)
add_importlibs(d3dumdrt msvcrt kernel32 ntdll)
add_dependencies(d3dumdrt psdk)
add_cd_file(TARGET d3dumdrt DESTINATION reactos/system32 FOR all)
