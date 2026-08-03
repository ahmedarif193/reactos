
add_definitions(
    -D__WINESRC__
    -DWINETEST_USE_WINE_DEBUG
    -DWINETEST_USE_DBGSTR_LONGLONG)

add_executable(amstream_winetest amstream.c testlist.c rsrc.rc)
target_include_directories(amstream_winetest BEFORE PRIVATE
    ${REACTOS_SOURCE_DIR}/sdk/lib/3rdparty/strmbase11/include)
target_link_libraries(amstream_winetest strmbase11 strmiids uuid wine)
set_module_type(amstream_winetest win32cui)
add_importlibs(amstream_winetest ddraw ole32 user32 msvcrt kernel32 ntdll)
add_rostests_file(TARGET amstream_winetest)
add_dependencies(amstream_winetest dxsdk)
