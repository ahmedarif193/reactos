spec2def(d3d10_1.dll d3d10_1.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    d3d10_1_main.c)

add_library(d3d10_1 MODULE
    ${SOURCE}
    version.rc
    ${CMAKE_CURRENT_BINARY_DIR}/d3d10_1.def
    ${CMAKE_CURRENT_BINARY_DIR}/d3d10_1_stubs.c)

set_module_type(d3d10_1 win32dll)
target_compile_definitions(d3d10_1 PRIVATE __WINESRC__)
target_link_libraries(d3d10_1 dx10guid uuid wine)
add_importlibs(d3d10_1 d3d10 d3d10core dxgi msvcrt kernel32 ntdll)
add_dependencies(d3d10_1 d3d_idl_headers)
add_cd_file(TARGET d3d10_1 DESTINATION reactos/system32 FOR all)
set_wine_module_FIXME(d3d10_1)
