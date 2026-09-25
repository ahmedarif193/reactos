add_definitions(
    -DUSE_WINE_TODOS
    -DWINETEST_WAIT_CHILD_PROCESS_INFO
    -D__WINESRC__
    -DWINETEST_USE_WINE_DEBUG
    -DWINETEST_USE_DBGSTR_LONGLONG
    -DWINETEST_USE_DBG_SPRINTF)

add_executable(mfplat_winetest
    mfplat.c
    resource.rc
    testlist.c)
target_link_libraries(mfplat_winetest mfuuid strmiids uuid wine pathcch)
set_module_type(mfplat_winetest win32cui)
add_importlibs(mfplat_winetest ole32 mfplat user32 d3d9 dxva2 propsys msvcrt kernel32 ntdll)
add_rostests_file(TARGET mfplat_winetest)
