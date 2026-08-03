add_definitions(-DUSE_WINE_TODOS)

add_executable(iccvid_winetest iccvid.c testlist.c)
set_module_type(iccvid_winetest win32cui)
add_importlibs(iccvid_winetest msvfw32 msvcrt kernel32)
add_rostests_file(TARGET iccvid_winetest)
