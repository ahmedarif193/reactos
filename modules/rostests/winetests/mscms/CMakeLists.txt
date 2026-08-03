
add_executable(mscms_winetest profile.c testlist.c)
set_module_type(mscms_winetest win32cui)
add_importlibs(mscms_winetest mscms user32 advapi32 msvcrt kernel32)
add_rostests_file(TARGET mscms_winetest)
