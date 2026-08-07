add_definitions(
    -DUSE_WINE_TODOS
    -D_USE_MATH_DEFINES)

set(PCH_SKIP_SOURCE
    d3d9ex.c
    device.c
    testlist.c)

add_executable(d3d9_winetest
    d3d9ex.c
    device.c
    stateblock.c
    visual.c
    testlist.c)

target_compile_definitions(d3d9_winetest PRIVATE CONST_VTABLE)
target_link_libraries(d3d9_winetest uuid)
set_module_type(d3d9_winetest win32cui)
add_importlibs(d3d9_winetest d3d9 user32 gdi32 msvcrt kernel32)
add_pch(d3d9_winetest precomp.h "${PCH_SKIP_SOURCE}")
add_rostests_file(TARGET d3d9_winetest)
