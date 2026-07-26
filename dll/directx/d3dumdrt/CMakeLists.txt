# The runtime half of the user-mode driver contract is compiled at the same
# WDDM interface version as the drivers it serves: the callback table is
# version-guarded, and a runtime that built a shorter table than the driver
# reads would have the driver walk off its end.
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
