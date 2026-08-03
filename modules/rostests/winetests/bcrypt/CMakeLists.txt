
remove_definitions(-D_WIN32_WINNT=${REACTOS_TARGET_NT} -DWINVER=${REACTOS_TARGET_NT})
add_executable(bcrypt_winetest bcrypt.c testlist.c)
set_module_type(bcrypt_winetest win32cui)
add_importlibs(bcrypt_winetest bcrypt user32 advapi32 msvcrt kernel32)
add_rostests_file(TARGET bcrypt_winetest)
