add_definitions(
    -DUSE_WINE_TODOS
    -D__WINESRC__
    -DWINETEST_USE_WINE_DEBUG
    -DWINETEST_USE_DBGSTR_LONGLONG
    -DWINETEST_USE_DBG_SPRINTF)

add_executable(rtworkq_winetest
    rtworkq.c
    testlist.c)
target_link_libraries(rtworkq_winetest wine)
set_module_type(rtworkq_winetest win32cui)
add_importlibs(rtworkq_winetest rtworkq ole32 msvcrt kernel32 ntdll)
add_rostests_file(TARGET rtworkq_winetest)
