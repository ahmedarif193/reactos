
add_executable(uxtheme_winetest system.c testlist.c)
target_compile_definitions(uxtheme_winetest PRIVATE __WINESRC__)
set_module_type(uxtheme_winetest win32cui)
add_importlibs(uxtheme_winetest advapi32 user32 gdi32 uxtheme msvcrt kernel32)
add_rostests_file(TARGET uxtheme_winetest)
