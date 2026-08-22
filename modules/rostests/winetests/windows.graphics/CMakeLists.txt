add_definitions(-DUSE_WINE_TODOS -D__WINESRC__)

add_executable(windows_graphics_winetest graphics.c testlist.c)
set_module_type(windows_graphics_winetest win32cui)
add_importlibs(windows_graphics_winetest combase msvcrt kernel32)
add_rostests_file(TARGET windows_graphics_winetest RENAME wgfx.exe)
