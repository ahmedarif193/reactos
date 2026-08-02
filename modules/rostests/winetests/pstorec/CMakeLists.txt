
add_executable(pstorec_winetest pstorec.c testlist.c)
set_module_type(pstorec_winetest win32cui)
add_importlibs(pstorec_winetest msvcrt kernel32)
add_rostests_file(TARGET pstorec_winetest)
