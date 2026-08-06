
add_executable(dmusic_winetest
    dmusic.c
    testlist.c)

target_compile_definitions(dmusic_winetest PRIVATE WINETEST_USE_DBGSTR_LONGLONG)
target_link_libraries(dmusic_winetest uuid)
set_module_type(dmusic_winetest win32cui)
add_importlibs(dmusic_winetest oleaut32 ole32 user32 dsound msvcrt kernel32)
add_rostests_file(TARGET dmusic_winetest)
