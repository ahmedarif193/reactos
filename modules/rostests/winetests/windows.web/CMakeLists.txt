add_definitions(-DUSE_WINE_TODOS -DWINETEST_USE_DBGSTR_HSTRING -DWINETEST_USE_WINE_DEBUG -D__WINESRC__)

add_executable(windows_web_winetest web.c testlist.c)
set_module_type(windows_web_winetest win32cui)
target_link_libraries(windows_web_winetest wine)
add_importlibs(windows_web_winetest combase msvcrt kernel32 ntdll)
add_rostests_file(TARGET windows_web_winetest RENAME wweb.exe)
