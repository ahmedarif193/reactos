
list(APPEND SOURCE
    shortcut.c)

add_executable(shdocvw_winetest ${SOURCE})
target_link_libraries(shdocvw_winetest winetestlib)
set_module_type(shdocvw_winetest win32cui)
add_importlibs(shdocvw_winetest shell32 ole32 oleaut32 advapi32 msvcrt kernel32)
add_rostests_file(TARGET shdocvw_winetest)
