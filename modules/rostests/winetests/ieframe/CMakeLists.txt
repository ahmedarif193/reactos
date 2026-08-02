list(APPEND SOURCE
    ie.c
    intshcut.c
    webbrowser.c)

add_executable(ieframe_winetest ${SOURCE})
set_module_type(ieframe_winetest win32cui)
target_link_libraries(ieframe_winetest uuid winetestlib)
add_importlibs(ieframe_winetest ieframe ole32 oleaut32 user32 gdi32 advapi32 msvcrt kernel32)
add_rostests_file(TARGET ieframe_winetest)
