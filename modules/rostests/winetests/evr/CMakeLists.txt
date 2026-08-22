add_definitions(
    -DUSE_WINE_TODOS
    -D__WINESRC__
    -DWINETEST_USE_WINE_DEBUG
    -DWINETEST_USE_DBGSTR_LONGLONG
    -DWINETEST_USE_DBG_SPRINTF)

add_executable(evr_winetest
    evr.c
    resource.rc
    testlist.c)
target_link_libraries(evr_winetest mfuuid strmiids uuid dxguid wine)
set_module_type(evr_winetest win32cui)
add_importlibs(evr_winetest dxva2 mfplat mf ole32 oleaut32 evr d3d9 user32 msvcrt kernel32 ntdll)
add_rostests_file(TARGET evr_winetest)
