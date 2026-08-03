
add_definitions(-DWINETEST_USE_DBGSTR_LONGLONG)

add_executable(dwrite_winetest
    analyzer.c
    font.c
    layout.c
    resource.rc
    testlist.c)

target_compile_definitions(dwrite_winetest PRIVATE __WINESRC__)
target_include_directories(dwrite_winetest BEFORE PRIVATE ${REACTOS_SOURCE_DIR}/sdk/include/wine)
set_module_type(dwrite_winetest win32cui)
add_importlibs(dwrite_winetest dwrite gdi32 user32 msvcrt kernel32)
add_rostests_file(TARGET dwrite_winetest)
