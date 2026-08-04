spec2def(d2d1.dll d2d1.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    bitmap.c
    bitmap_render_target.c
    brush.c
    command_list.c
    dc_render_target.c
    device.c
    effect.c
    factory.c
    geometry.c
    hwnd_render_target.c
    layer.c
    mesh.c
    state_block.c
    stroke.c
    wic_render_target.c
    d2d1_private.h)

add_library(d2d1 MODULE
    ${SOURCE}
    version.rc
    ${CMAKE_CURRENT_BINARY_DIR}/d2d1.def
    ${CMAKE_CURRENT_BINARY_DIR}/d2d1_stubs.c)

set_module_type(d2d1 win32dll)
target_compile_definitions(d2d1 PRIVATE __WINESRC__ _USE_MATH_DEFINES isinf=__builtin_isinf)
target_link_libraries(d2d1 dx10guid uuid wine)
add_importlibs(d2d1 d3d10_1 d3dcompiler_47 gdi32 user32 advapi32 msvcrt ucrtbase kernel32 ntdll)
add_delay_importlibs(d2d1 dwrite xmllite ole32)
add_dependencies(d2d1 d3d_idl_headers psdk)
add_cd_file(TARGET d2d1 DESTINATION reactos/system32 FOR all)
set_wine_module_FIXME(d2d1)
