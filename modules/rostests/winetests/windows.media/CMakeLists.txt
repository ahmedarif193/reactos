add_definitions(-DUSE_WINE_TODOS -DWINETEST_USE_DBGSTR_HSTRING -D__WINESRC__)

add_executable(windows_media_winetest media.c testlist.c)
set_module_type(windows_media_winetest win32cui)
add_importlibs(windows_media_winetest combase msvcrt kernel32 ntdll)
add_rostests_file(TARGET windows_media_winetest RENAME wmed.exe)
