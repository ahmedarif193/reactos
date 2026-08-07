add_definitions(
    -DUSE_WINE_TODOS
    -D_USE_MATH_DEFINES)

set(PCH_SKIP_SOURCE testlist.c)

add_executable(d3d8_winetest
    device.c
    stateblock.c
    visual.c
    testlist.c)

target_compile_definitions(d3d8_winetest PRIVATE CONST_VTABLE)
target_include_directories(d3d8_winetest BEFORE PRIVATE ${REACTOS_SOURCE_DIR}/sdk/include/wine)
target_link_libraries(d3d8_winetest uuid dxguid)
set_module_type(d3d8_winetest win32cui)
add_importlibs(d3d8_winetest d3d8 user32 gdi32 msvcrt kernel32)
add_pch(d3d8_winetest precomp.h "${PCH_SKIP_SOURCE}")
add_rostests_file(TARGET d3d8_winetest)
