
list(APPEND SOURCE
    capture.c
    ds3d8.c
    ds3d.c
    dsound8.c
    dsound.c
    duplex.c
    propset.c)

list(APPEND PCH_SKIP_SOURCE
    testlist.c)

add_executable(dsound_winetest
    ${SOURCE}
    ${PCH_SKIP_SOURCE})

target_compile_definitions(dsound_winetest PRIVATE COBJMACROS= WINETEST_USE_DBGSTR_LONGLONG)
target_link_libraries(dsound_winetest wine uuid dxguid dmoguids)
set_module_type(dsound_winetest win32cui)
add_importlibs(dsound_winetest dsound msdmo ole32 version user32 msvcrt kernel32)
add_rostests_file(TARGET dsound_winetest)
