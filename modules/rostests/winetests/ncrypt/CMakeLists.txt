add_executable(ncrypt_winetest ncrypt.c testlist.c)
set_module_type(ncrypt_winetest win32cui)
add_importlibs(ncrypt_winetest ncrypt msvcrt kernel32)
add_rostests_file(TARGET ncrypt_winetest)
