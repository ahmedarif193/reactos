
add_executable(wldap32_winetest ber.c parse.c testlist.c)
set_module_type(wldap32_winetest win32cui)
add_importlibs(wldap32_winetest crypt32 wldap32 netapi32 msvcrt kernel32)
add_rostests_file(TARGET wldap32_winetest)
