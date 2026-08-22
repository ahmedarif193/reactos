add_definitions(
    -DUSE_WINE_TODOS
    -D__WINESRC__
    -DWINETEST_USE_WINE_DEBUG
    -DWINETEST_USE_DBGSTR_LONGLONG
    -DWINETEST_USE_DBG_SPRINTF)

add_executable(dxva2_winetest
    dxva2.c
    testlist.c)
target_link_libraries(dxva2_winetest wine)
set_module_type(dxva2_winetest win32cui)
add_importlibs(dxva2_winetest dxva2 user32 d3d9 ole32 msvcrt kernel32 ntdll)
add_rostests_file(TARGET dxva2_winetest)
