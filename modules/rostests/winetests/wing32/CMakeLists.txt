
add_executable(wing32_winetest wing32.c testlist.c)
set_module_type(wing32_winetest win32cui)
add_importlibs(wing32_winetest wing32 gdi32 msvcrt kernel32)
add_rostests_file(TARGET wing32_winetest)
