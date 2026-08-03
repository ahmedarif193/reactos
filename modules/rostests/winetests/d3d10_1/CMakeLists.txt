add_definitions(
    -DUSE_WINE_TODOS
    -D__WINESRC__
    -DD3D_COMPILER_VERSION=0)

add_executable(d3d10_1_winetest
    d3d10_1.c
    reflection.c
    testlist.c)

target_link_libraries(d3d10_1_winetest dx10guid uuid)
set_module_type(d3d10_1_winetest win32cui)
add_dependencies(d3d10_1_winetest d3d_idl_headers)
add_importlibs(d3d10_1_winetest d3d10_1 user32 msvcrt kernel32)
add_rostests_file(TARGET d3d10_1_winetest)
