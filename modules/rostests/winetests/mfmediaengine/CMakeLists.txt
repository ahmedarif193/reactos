add_definitions(
    -DUSE_WINE_TODOS
    -D__WINESRC__
    -DWINETEST_USE_WINE_DEBUG
    -DWINETEST_USE_DBGSTR_LONGLONG
    -DWINETEST_USE_DBG_SPRINTF)

add_executable(mfmediaengine_winetest
    mfmediaengine.c
    resource.rc
    testlist.c)
target_link_libraries(mfmediaengine_winetest mfuuid uuid dxguid wine)
set_module_type(mfmediaengine_winetest win32cui)
add_importlibs(mfmediaengine_winetest ole32 mf mfplat oleaut32 user32 msvcrt kernel32 ntdll)
add_rostests_file(TARGET mfmediaengine_winetest)
