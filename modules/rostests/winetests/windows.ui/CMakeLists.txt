add_definitions(-DUSE_WINE_TODOS -D__WINESRC__)

add_executable(windows_ui_winetest corewindow.c uisettings.c testlist.c)
set_module_type(windows_ui_winetest win32cui)
add_importlibs(windows_ui_winetest combase advapi32 user32 msvcrt kernel32)
add_rostests_file(TARGET windows_ui_winetest RENAME wui.exe)
