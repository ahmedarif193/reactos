add_definitions(
    -DUSE_WINE_TODOS
    -DWINETEST_USE_DBGSTR_LONGLONG
    -D__WINESRC__)

add_executable(dxgi_winetest
    dxgi.c
    testlist.c)

target_link_libraries(dxgi_winetest dx10guid uuid)
target_include_directories(dxgi_winetest BEFORE PRIVATE ${REACTOS_SOURCE_DIR}/sdk/include/wine)
set_module_type(dxgi_winetest win32cui)
add_dependencies(dxgi_winetest d3d_idl_headers)
add_importlibs(dxgi_winetest d3d10_1 dxgi user32 msvcrt kernel32)
add_rostests_file(TARGET dxgi_winetest)
