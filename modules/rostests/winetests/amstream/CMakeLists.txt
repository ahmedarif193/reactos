
add_definitions(
    -D__WINESRC__
    -DWINETEST_USE_WINE_DEBUG
    -DWINETEST_USE_DBGSTR_LONGLONG)

add_executable(amstream_winetest amstream.c testlist.c rsrc.rc)
target_compile_definitions(amstream_winetest PRIVATE USE_WINE_TODOS)
target_link_libraries(amstream_winetest strmbase strmiids uuid wine)
set_module_type(amstream_winetest win32cui)
add_importlibs(amstream_winetest ddraw ole32 user32 msvcrt kernel32 ntdll)
add_rostests_file(TARGET amstream_winetest)
add_dependencies(amstream_winetest dxsdk)
