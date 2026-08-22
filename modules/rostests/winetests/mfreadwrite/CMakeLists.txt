add_definitions(
    -DUSE_WINE_TODOS
    -D__WINESRC__
    -DWINETEST_USE_WINE_DEBUG
    -DWINETEST_USE_DBGSTR_LONGLONG
    -DWINETEST_USE_DBG_SPRINTF)

add_executable(mfreadwrite_winetest
    mfplat.c
    resource.rc
    testlist.c)
target_link_libraries(mfreadwrite_winetest mfuuid wine)
set_module_type(mfreadwrite_winetest win32cui)
add_importlibs(mfreadwrite_winetest ole32 user32 d3d11 d3d9 dxva2 mfplat mf mfreadwrite propsys msvcrt kernel32 ntdll)
add_rostests_file(TARGET mfreadwrite_winetest)
