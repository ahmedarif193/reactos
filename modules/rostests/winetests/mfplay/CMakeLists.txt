add_definitions(
    -DUSE_WINE_TODOS
    -D__WINESRC__
    -DWINETEST_USE_WINE_DEBUG
    -DWINETEST_USE_DBGSTR_LONGLONG
    -DWINETEST_USE_DBG_SPRINTF)

add_executable(mfplay_winetest
    mfplay.c
    resource.rc
    testlist.c)
target_link_libraries(mfplay_winetest uuid mfuuid wine)
set_module_type(mfplay_winetest win32cui)
add_importlibs(mfplay_winetest mfplay user32 mfplat ole32 msvcrt kernel32 ntdll)
add_rostests_file(TARGET mfplay_winetest)
