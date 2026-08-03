spec2def(d3d10core.dll d3d10core.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    d3d10core_main.c)

add_library(d3d10core MODULE
    ${SOURCE}
    version.rc
    ${CMAKE_CURRENT_BINARY_DIR}/d3d10core.def)

set_module_type(d3d10core win32dll)
target_compile_definitions(d3d10core PRIVATE __WINESRC__)
target_link_libraries(d3d10core dx10guid uuid wine)
add_importlibs(d3d10core d3d11 dxgi msvcrt kernel32 ntdll)
add_dependencies(d3d10core d3d_idl_headers)
add_cd_file(TARGET d3d10core DESTINATION reactos/system32 FOR all)
set_wine_module_FIXME(d3d10core)
