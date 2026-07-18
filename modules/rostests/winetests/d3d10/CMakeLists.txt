add_definitions(
    -DUSE_WINE_TODOS
    -DD3D_COMPILER_VERSION=0)

set(PCH_SKIP_SOURCE testlist.c)

add_executable(d3d10_winetest
    device.c
    effect.c
    reflection.c
    testlist.c)

target_link_libraries(d3d10_winetest dx10guid uuid)
set_module_type(d3d10_winetest win32cui)
add_dependencies(d3d10_winetest d3d_idl_headers)
add_importlibs(d3d10_winetest d3d10 d3dcompiler_43 msvcrt kernel32)
add_pch(d3d10_winetest precomp.h "${PCH_SKIP_SOURCE}")
add_rostests_file(TARGET d3d10_winetest)
