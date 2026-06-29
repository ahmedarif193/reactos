
spec2def(d3d10.dll d3d10.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    d3d10_main.c)

add_library(d3d10 MODULE
    ${SOURCE}
    ${CMAKE_CURRENT_BINARY_DIR}/d3d10_stubs.c
    ${CMAKE_CURRENT_BINARY_DIR}/d3d10.def)

set_module_type(d3d10 win32dll)
target_link_libraries(d3d10 wine)
add_importlibs(d3d10 msvcrt kernel32 ntdll)
add_cd_file(TARGET d3d10 DESTINATION reactos/system32 FOR all)
