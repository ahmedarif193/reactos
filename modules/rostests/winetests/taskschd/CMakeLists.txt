
add_executable(taskschd_winetest
    scheduler.c
    testlist.c)
set_module_type(taskschd_winetest win32cui)
target_link_libraries(taskschd_winetest wine)
add_importlibs(taskschd_winetest advapi32 oleaut32 ole32 msvcrt kernel32 ntdll)
add_rostests_file(TARGET taskschd_winetest)
