
spec2def(d3d10_1.dll d3d10_1.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    d3d10_1_main.c)

add_library(d3d10_1 MODULE
    ${SOURCE}
    ${CMAKE_CURRENT_BINARY_DIR}/d3d10_1_stubs.c
    ${CMAKE_CURRENT_BINARY_DIR}/d3d10_1.def)

set_module_type(d3d10_1 win32dll)
target_link_libraries(d3d10_1 wine)
add_importlibs(d3d10_1 msvcrt kernel32 ntdll)
add_cd_file(TARGET d3d10_1 DESTINATION reactos/system32 FOR all)
