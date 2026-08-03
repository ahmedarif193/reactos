
add_definitions(
    -D__WINESRC__
    -DWINETEST_USE_WINE_DEBUG
    -DWINETEST_USE_DBGSTR_LONGLONG)

add_executable(qedit_winetest
    mediadet.c
    nullrenderer.c
    samplegrabber.c
    timeline.c
    testlist.c
    qedit.rc)

target_link_libraries(qedit_winetest strmbase strmiids uuid wine)
set_module_type(qedit_winetest win32cui)
add_importlibs(qedit_winetest oleaut32 ole32 user32 msvcrt kernel32 ntdll)
add_rostests_file(TARGET qedit_winetest)
