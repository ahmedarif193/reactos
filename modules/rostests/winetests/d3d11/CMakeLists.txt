add_definitions(
    -DUSE_WINE_TODOS
    -D__WINESRC__)

set(PCH_SKIP_SOURCE testlist.c)

add_executable(d3d11_winetest
    d3d11.c
    testlist.c)

target_include_directories(d3d11_winetest BEFORE PRIVATE ${REACTOS_SOURCE_DIR}/sdk/include/wine)
target_link_libraries(d3d11_winetest dx10guid uuid)
set_module_type(d3d11_winetest win32cui)
add_dependencies(d3d11_winetest d3d_idl_headers)
add_importlibs(d3d11_winetest d3d11 dxgi d3dcompiler_43 user32 gdi32 msvcrt kernel32 ntdll)
add_pch(d3d11_winetest precomp.h "${PCH_SKIP_SOURCE}")
add_rostests_file(TARGET d3d11_winetest)
