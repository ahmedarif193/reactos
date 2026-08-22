add_definitions(-DUSE_WINE_TODOS -DWINETEST_USE_DBGSTR_HSTRING -D__WINESRC__)

add_executable(windows_ui_xaml_winetest xaml.c testlist.c)
set_module_type(windows_ui_xaml_winetest win32cui)
add_importlibs(windows_ui_xaml_winetest combase msvcrt kernel32 ntdll)
add_rostests_file(TARGET windows_ui_xaml_winetest RENAME wxaml.exe)
