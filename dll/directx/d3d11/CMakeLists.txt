
spec2def(d3d11.dll d3d11.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    d3d11_main.c
    device.c
    context.c
    state.c
    buffer.c
    texture.c
    view.c
    shader.c
    inputlayout.c
    query.c
    utils.c)

add_library(d3d11 MODULE
    ${SOURCE}
    d3d11.rc
    ${CMAKE_CURRENT_BINARY_DIR}/d3d11_stubs.c
    ${CMAKE_CURRENT_BINARY_DIR}/d3d11.def)

set_module_type(d3d11 win32dll)
target_link_libraries(d3d11 wine)
add_dependencies(d3d11 psdk d3d_idl_headers)
add_importlibs(d3d11 dxgi ole32 gdi32 user32 msvcrt kernel32 ntdll)
add_cd_file(TARGET d3d11 DESTINATION reactos/system32 FOR all)
