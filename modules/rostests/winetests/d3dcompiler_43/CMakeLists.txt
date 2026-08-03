
add_definitions(
    -DUSE_WINE_TODOS
    -D_USE_MATH_DEFINES
    -DD3D_COMPILER_VERSION=43
    -D__WINESRC__)

add_executable(d3dcompiler_43_winetest
    asm.c
    blob.c
    hlsl_d3d11.c
    hlsl_d3d9.c
    reflection.c
    testlist.c)

target_link_libraries(d3dcompiler_43_winetest dx10guid uuid)
set_module_type(d3dcompiler_43_winetest win32cui)
add_dependencies(d3dcompiler_43_winetest d3d_idl_headers)
add_importlibs(d3dcompiler_43_winetest d3d9 d3dcompiler_43 user32 msvcrt kernel32)
add_rostests_file(TARGET d3dcompiler_43_winetest)
