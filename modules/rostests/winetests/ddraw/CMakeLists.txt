add_definitions(
    -DUSE_WINE_TODOS
    -D_USE_MATH_DEFINES)

set(PCH_SKIP_SOURCE testlist.c)

add_executable(ddraw_winetest
    d3d.c
    ddraw1.c
    ddraw2.c
    ddraw4.c
    ddraw7.c
    ddrawmodes.c
    dsurface.c
    refcount.c
    visual.c
    testlist.c)

target_compile_definitions(ddraw_winetest PRIVATE CONST_VTABLE)
target_link_libraries(ddraw_winetest uuid dxguid)
set_module_type(ddraw_winetest win32cui)
add_importlibs(ddraw_winetest ddraw user32 gdi32 ole32 msvcrt kernel32)
add_pch(ddraw_winetest precomp.h "${PCH_SKIP_SOURCE}")
add_rostests_file(TARGET ddraw_winetest)
