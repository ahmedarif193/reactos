
add_definitions(
    -D__WINESRC__
    -DWINETEST_USE_WINE_DEBUG
    -DWINETEST_USE_DBGSTR_LONGLONG)

list(APPEND SOURCE
    container.c
    provider.c
    testlist.c)

add_executable(dxdiagn_winetest ${SOURCE})
target_link_libraries(dxdiagn_winetest wine)
set_module_type(dxdiagn_winetest win32cui)
add_importlibs(dxdiagn_winetest oleaut32 ole32 msvcrt kernel32 ntdll)
add_rostests_file(TARGET dxdiagn_winetest)
