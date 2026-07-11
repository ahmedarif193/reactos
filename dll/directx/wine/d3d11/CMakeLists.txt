spec2def(d3d11.dll d3d11.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    d3d11_main.c)

add_library(d3d11 MODULE
    ${SOURCE}
    version.rc
    ${CMAKE_CURRENT_BINARY_DIR}/d3d11.def
    ${CMAKE_CURRENT_BINARY_DIR}/d3d11_stubs.c)

set_module_type(d3d11 win32dll)
target_link_libraries(d3d11 uuid wine)
add_importlibs(d3d11 dxgi gdi32 msvcrt kernel32 ntdll)
# Shipped from dxvk-artifacts instead (dll/3rdparty/dxvk); stub kept for importlib.
# add_cd_file(TARGET d3d11 DESTINATION reactos/system32 FOR all)
set_wine_module_FIXME(d3d11)
