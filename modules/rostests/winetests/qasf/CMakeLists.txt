add_definitions(
    -DUSE_WINE_TODOS
    -D__WINESRC__
    -DWINETEST_USE_WINE_DEBUG
    -DWINETEST_USE_DBGSTR_LONGLONG
    -DWINETEST_USE_DBG_SPRINTF)

add_executable(qasf_winetest
    asfreader.c
    dmowrapper.c
    resource.rc
    testlist.c)
target_link_libraries(qasf_winetest strmbase dmoguids strmiids uuid wine)
set_module_type(qasf_winetest win32cui)
add_importlibs(qasf_winetest msdmo ole32 user32 msvcrt kernel32 ntdll)
add_rostests_file(TARGET qasf_winetest)
