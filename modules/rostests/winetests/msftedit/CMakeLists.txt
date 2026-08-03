add_executable(msftedit_winetest richole.c testlist.c)
set_module_type(msftedit_winetest win32cui)
add_importlibs(msftedit_winetest ole32 oleaut32 user32 msvcrt kernel32)
add_rostests_file(TARGET msftedit_winetest)
