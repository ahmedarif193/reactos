add_definitions(
    -DUSE_WINE_TODOS
    -D__WINESRC__
    -DWINETEST_USE_WINE_DEBUG
    -DWINETEST_USE_DBGSTR_LONGLONG
    -DWINETEST_USE_DBG_SPRINTF)

add_executable(mfsrcsnk_winetest
    mfsrcsnk.c
    resource.rc
    testlist.c)
target_link_libraries(mfsrcsnk_winetest uuid mfuuid wine)
set_module_type(mfsrcsnk_winetest win32cui)
add_importlibs(mfsrcsnk_winetest ole32 mfsrcsnk mfplat mf msvcrt kernel32 ntdll)
add_rostests_file(TARGET mfsrcsnk_winetest)
