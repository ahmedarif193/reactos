
add_definitions(-DUSE_WINE_TODOS)
add_executable(dssenh_winetest dssenh.c testlist.c)
set_module_type(dssenh_winetest win32cui)
add_importlibs(dssenh_winetest advapi32 msvcrt kernel32)
add_rostests_file(TARGET dssenh_winetest)
